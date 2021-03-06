#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#include <windows.h>
#endif

#include "quickobj.h"
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <ccl/FileInfo.h>
#include <ccl/StringUtils.h>

#include <fstream>
#include <GL/glew.h>
#include <GL/gl.h>
#include <ip/jpgwrapper.h>

#include <errno.h>

#ifndef GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03
#endif
#ifndef GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#endif

// S3TC/DXT (GL_EXT_texture_compression_s3tc) : Most desktop/console gpus
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// ATC (GL_AMD_compressed_ATC_texture) : Qualcomm/Adreno based gpus
#ifndef ATC_RGB_AMD
#define ATC_RGB_AMD 0x8C92
#endif
#ifndef ATC_RGBA_EXPLICIT_ALPHA_AMD
#define ATC_RGBA_EXPLICIT_ALPHA_AMD 0x8C93
#endif
#ifndef ATC_RGBA_INTERPOLATED_ALPHA_AMD
#define ATC_RGBA_INTERPOLATED_ALPHA_AMD 0x87EE
#endif

// ETC1 (OES_compressed_ETC1_RGB8_texture) : All OpenGL ES chipsets
#ifndef ETC1_RGB8
#define ETC1_RGB8 0x8D64
#endif



namespace cognitics {

    bool fileToENU(Cognitics::CoordinateSystems::EllipsoidTangentPlane *ltp_ellipsoid, OGRCoordinateTransformation* coordTrans, float &x, float &y, float &z)
    {
        double local_x = 0, local_y = 0, local_z = 0;
        double dx = x;
        double dy = y;
        double dz = z;
        coordTrans->Transform(1, &dx, &dy, &dz);
        ltp_ellipsoid->GeodeticToLocal(dy,
            dx,
            dz,
            local_x,
            local_y,
            local_z);
        x = local_x;
        y = local_y;
        z = local_z;
        return true;
    }

    bool QuickObj::parseOBJ(bool loadTextures)
    {
        log.init("QuickObj", this);
        ccl::FileInfo fi(objFilename);
        std::string objFilePath = fi.getDirName();
        //Get the file size
        auto fileSize = ccl::getFileSize(objFilename);
        if (fileSize > (1024 * 1024 * 1024))
        {
            log << "File size of " << fileSize << " for " << objFilename << " exceeds the max of 1gb" << log.endl;
            return false;
        }
        //Make a buffer for the text
        char *fileContents = new char[fileSize + 1];
        //Open the file
        FILE *f = fopen(objFilename.c_str(), "rb");
        if (!f)
        {
            delete[] fileContents;
            log << "Unable to open " << objFilename << ". error: " << strerror(errno) << log.endl;
            return false;
        }
        //Read the entire file
        fread(fileContents, 1, fileSize, f);
        int pos = 0;

        //OBJ indexes start at 1, so we put a placeholder in 0
        QuickVert placeholder3;
        placeholder3.x = 0; placeholder3.y = 0; placeholder3.z = 0;
        verts.push_back(placeholder3);
        uvs.push_back(placeholder3);
        norms.push_back(placeholder3);

        while (pos < fileSize)
        {
            int lineStart = pos;

            //line by line
            while (fileContents[pos] != 0x0a && fileContents[pos] != 0x0d && fileContents[pos] != 0 && pos < fileSize)
            {
                pos++;
            }
            //Null terminate the line
            fileContents[pos] = 0;

            int lineEnd = pos;
            //Process between lineStart and lineEnd
            char *tok = strtok(fileContents + lineStart, " ");
            if (strcmp(tok, "v") == 0)
            {
                char *x = strtok(NULL, " ");
                if (!x)
                    continue;
                char *y = strtok(NULL, " ");
                if (!y)
                    continue;
                QuickVert v;

                v.x = atof(x) + srs.offsetPt.X();
                v.y = atof(y) + srs.offsetPt.Y();
                char *z = strtok(NULL, " ");
                if (!z)
                    v.z = srs.offsetPt.Z();
                else
                    v.z = atof(z) + srs.offsetPt.Z();

                /*
                double tmpv = v.x;
                v.x = v.y;
                v.y = tmpv;
                */
                /*if(v.x < (srs.offsetPt.X()/2))
                {
                    printf("!!!");
                }*/
                minX = std::min<float>(minX, v.x);
                minY = std::min<float>(minY, v.y);
                minZ = std::min<float>(minZ, v.z);

                maxX = std::max<float>(maxX, v.x);
                maxY = std::max<float>(maxY, v.y);
                maxZ = std::max<float>(maxZ, v.z);

                verts.push_back(v);
            }
            else if (strcmp(tok, "vt") == 0)
            {
                char *x = strtok(NULL, " ");
                if (!x)
                    continue;
                char *y = strtok(NULL, " ");
                if (!y)
                    continue;
                QuickVert vt;
                vt.x = atof(x);
                vt.y = atof(y);
                vt.z = 0;
                uvs.push_back(vt);
            }
            else if (strcmp(tok, "vn") == 0)
            {
                char *x = strtok(NULL, " ");
                if (!x)
                    continue;
                char *y = strtok(NULL, " ");
                if (!y)
                    continue;
                QuickVert vn;
                vn.x = atof(x);
                vn.y = atof(y);
                char *z = strtok(NULL, " ");
                if (!z)
                    vn.z = 0;
                else
                    vn.z = atof(z);
                norms.push_back(vn);
            }
            else if (strcmp(tok, "f") == 0)
            {
                tok = strtok(NULL, " ");
                while (tok)
                {
                    char *vp = tok;
                    char *vtp = NULL;
                    char *vnp = NULL;
                    while (*tok != 0)
                    {
                        if (*tok == '/')
                        {
                            //Terminate the previous pointer
                            if (vtp == NULL)
                                vtp = tok + 1;
                            else if (vnp == NULL)
                                vnp = tok + 1;
                            else
                                break;
                            *tok = 0;
                        }
                        tok++;
                    }
                    uint32_t vertId = atoi(vp);
                    vertIdxs.push_back(vertId);
                    auto &lastMesh = subMeshes.back();
                    lastMesh.vertIdxs.push_back(vertId);

                    if (vtp)
                    {
                        uint32_t uvId = atoi(vtp);
                        uvIdxs.push_back(uvId);
                        lastMesh.uvIdxs.push_back(uvId);
                    }
                    if (vnp)
                    {
                        uint32_t normId = atoi(vnp);
                        normIdxs.push_back(normId);
                        lastMesh.normIdxs.push_back(normId);
                    }
                    tok = strtok(NULL, " ");
                }
            }
            else if (strcmp(tok, "mtllib") == 0)
            {
                char *materialFile = strtok(NULL, " ");
                materialFilename = ccl::joinPaths(objFilePath, materialFile);
                if (loadTextures)
                    parseMtlFile(materialFilename);
            }
            else if (strcmp(tok, "usemtl") == 0)
            {
                //Defines a new material from this point on
                char *material = strtok(NULL, " ");
                materialName = std::string(material);
                QuickSubMesh submesh;
                submesh.materialName = materialName;
                subMeshes.push_back(submesh);
                auto lastMesh = subMeshes.back();
            }
            //Skip and remaining cr/lf
            while ((pos < fileSize) &&
                (fileContents[pos] == 0x0a || fileContents[pos] == 0x0d || fileContents[pos] == 0))
            {
                pos++;
            }

        }//end of while parsing lines
        fclose(f);
        _isValid = true;
        delete[] fileContents;
        //Transform if needed. If there is no WKT, assume it's already in ENU
        if (srs.srsWKT.size() > 0 && srs.srsWKT != "ENU")
        {
            OGRSpatialReference wgs;
            OGRCoordinateTransformation* coordTrans;
            wgs.SetFromUserInput("WGS84");
            OGRSpatialReference *file_srs = new OGRSpatialReference;

            const char *prjstr = srs.srsWKT.c_str();
            OGRErr err;
            if (prjstr[0] == '+')
            {
                err = file_srs->importFromProj4(prjstr);
            }
            else
            {
                err = file_srs->importFromWkt((char **)&prjstr);
            }

            if (err != OGRERR_NONE)
            {
                delete file_srs;
                return false;
            }
            coordTrans = OGRCreateCoordinateTransformation(file_srs, &wgs);

            double originLat = 0;
            double originLon = 0;
            double x = srs.offsetPt.X();
            double y = srs.offsetPt.Y();
            double z = srs.offsetPt.Z();
            //Use the offset for the origin
            coordTrans->Transform(1, &x, &y, &z);
            //now x,y,z should be geo
            originLat = y;
            originLon = x;
            std::stringstream ss;
            ss.precision(12);
            ss << "Using an origin of lat=" << originLat << " lon=" << originLon;
            auto ltp_ellipsoid = new Cognitics::CoordinateSystems::EllipsoidTangentPlane(originLat, originLon);

            double local_x = 0, local_y = 0, local_z = 0;

            for (int i = 1, ic = verts.size(); i < ic; i++)
            {
                QuickVert &vert = verts[i];
                fileToENU(ltp_ellipsoid, coordTrans, vert.x, vert.y, vert.z);
            }
            fileToENU(ltp_ellipsoid, coordTrans, minX, minY, minZ);
            fileToENU(ltp_ellipsoid, coordTrans, maxX, maxY, maxZ);
        }
        return true;
    }

    bool QuickObj::parseLMAB(bool loadTextures)
    {
        //Not implemented yet because the lmab file has a number of meshes
        //at different LODs, so it will require some restructuring to use.
        //for now, convert the lmab to obj using the lmbundle2ogj app
        return false;
    }

    QuickObj::QuickObj(const std::string &objFilename,
            const ObjSrs &srs,
            const std::string &_textureDirectory, 
            bool loadTextures) :
                objFilename(objFilename),
                textureDirectory(_textureDirectory),
                minX(FLT_MAX),maxX(-FLT_MAX),minY(FLT_MAX),
                maxY(-FLT_MAX),minZ(FLT_MAX),maxZ(-FLT_MAX),
                _isValid(false)
    {
        log.init("QuickObj",this);
        ccl::FileInfo fi(objFilename);
        std::string objFilePath = fi.getDirName();
        if(ccl::stringCompareNoCase(fi.getSuffix(),"obj")==0)
        {
            parseOBJ(loadTextures);
        }
        else if (ccl::stringCompareNoCase(fi.getSuffix(), "lmab")==0)
        {
            parseLMAB(loadTextures);
        }

        return;
        
    }

    void QuickObj::getBounds(float &minX, float &maxX,
                             float &minY, float &maxY,
                             float &minZ, float &maxZ)
    {
        minX = this->minX;
        minY = this->minY;
        minZ = this->minZ;

        maxX = this->maxX;
        maxY = this->maxY;
        maxZ = this->maxZ;
    }


    bool QuickObj::parseMtlFile(const std::string &mtlFilename)
    {
        Material material;
        std::string matName = "";
        std::ifstream infile;
        infile.open(mtlFilename, std::ios::in);
        std::string line;
        while (std::getline(infile, line))
        {
            line = ccl::trim_string(line);
            if (line.empty())
            {
                continue;
            }
            std::vector<std::string> parts = ccl::splitString(line, " ");
            if (parts.size() < 2)
            {
                continue;
            }
            if (parts[0] == "newmtl")
            {
                if (matName.size() > 0)
                {
                    materialMap[matName] = material;
                    material = Material();
                }
                matName = parts[1];
            }
            //Don't parse anything else until we have a material.
            else if (matName.size() > 0)
            {
                if (parts[0] == "Ka")
                {
                    if (parts.size() > 3)
                    {
                        if (parts[1] == "spectral")
                        {
                            log << "Spectral not a supported ambient value." << log.endl;
                        }
                        else if (parts[1] == "xyz")
                        {
                            log << "xyz not a supported ambient value." << log.endl;
                        }
                        else
                        {
                            double r = atof(parts[1].c_str());
                            double g = atof(parts[2].c_str());
                            double b = atof(parts[3].c_str());
                            material.ambient = Color(r, g, b);
                        }
                    }
                    else
                    {
                        log << "Not enough values for ambient [Ka] material record." << log.endl;
                    }
                }
                if (parts[0] == "Kd")
                {
                    if (parts.size() > 3)
                    {
                        if (parts[1] == "spectral")
                        {
                            log << "Spectral not a supported diffuse value." << log.endl;
                        }
                        else if (parts[1] == "xyz")
                        {
                            log << "xyz not a supported diffuse value." << log.endl;
                        }
                        else
                        {
                            double r = atof(parts[1].c_str());
                            double g = atof(parts[2].c_str());
                            double b = atof(parts[3].c_str());
                            material.diffuse = Color(r, g, b);
                        }
                    }
                    else
                    {
                        log << "Not enough values for ambient [Ka] material record." << log.endl;
                    }
                }
                if (parts[0] == "Ks")
                {
                    if (parts.size() > 3)
                    {
                        if (parts[1] == "spectral")
                        {
                            log << "Spectral not a supported specular value." << log.endl;
                        }
                        else if (parts[1] == "xyz")
                        {
                            log << "xyz not a supported specular value." << log.endl;
                        }
                        else
                        {
                            double r = atof(parts[1].c_str());
                            double g = atof(parts[2].c_str());
                            double b = atof(parts[3].c_str());
                            material.specular = Color(r, g, b);
                        }
                    }
                    else
                    {
                        log << "Not enough values for ambient [Ka] material record." << log.endl;
                    }
                }
                if (parts[0] == "d")
                {
                    //Dissolve unsupported
                }
                if (parts[0] == "Ns")
                {
                    //Specular Exponent Unsupported
                }
                if (parts[0] == "illum")
                {
                    /*
                        0        Color on and Ambient off
                        1        Color on and Ambient on
                        2        Highlight on
                        3        Reflection on and Ray trace on
                        4        Transparency: Glass on
                                Reflection: Ray trace on
                        5        Reflection: Fresnel on and Ray trace on
                        6        Transparency: Refraction on
                                Reflection: Fresnel off and Ray trace on
                        7        Transparency: Refraction on
                                Reflection: Fresnel on and Ray trace on
                        8        Reflection on and Ray trace off
                        9        Transparency: Glass on
                                Reflection: Ray trace off
                        10       Casts shadows onto invisible surfaces
                    */
                }
                if (parts[0] == "map_Kd")
                {
                    //Full path, relative to the mtl file (and the obj)
                    ccl::FileInfo fi(objFilename);
                    std::string objFilePath = fi.getDirName();
                    material.textureFile = ccl::joinPaths(objFilePath,parts[1]);
                }
            }
        }
        //Save the last one.
        if (matName.size() > 0)
        {
            materialMap[matName] = material;
        }
        return true;
    }



    Material::Material() : shine(0.0), transparency(0.0) 
    {
    }

    Material::~Material()
    {
    }

    bool Material::operator==(const Material &rhs) const
    {
        return (shine == rhs.shine)
            && (transparency == rhs.transparency)
            && (ambient == rhs.ambient)
            && (diffuse == rhs.diffuse)
            && (specular == rhs.specular)
            && (emission == rhs.emission);
    }

    bool Material::operator< (const Material &rhs) const
    {
        if(shine < rhs.shine)
            return true;
        if(shine > rhs.shine)
            return false;
        if(transparency < rhs.transparency)
            return true;
        if(transparency > rhs.transparency)
            return false;
        if(ambient < rhs.ambient)
            return true;
        if(ambient > rhs.ambient)
            return false;
        if(diffuse < rhs.diffuse)
            return true;
        if(diffuse > rhs.diffuse)
            return false;
        if(specular < rhs.specular)
            return true;
        if(specular > rhs.specular)
            return false;
        return emission < rhs.emission;
    }

    
    Color::Color(double R, double G, double B, double A)
        : initialized(true),r(R), g(G), b(B), a(A)
    {
    }
    
    Color::Color() : initialized(false),r(1),g(1),b(1),a(1)
    {
    }

    Color::~Color()
    {
    }

    bool Color::isInitialized() const
    {
        return initialized;
    }

    bool Color::operator==(const Color &rhs) const
    {
        return (r == rhs.r) && (g == rhs.g) && (b == rhs.b) && (a == rhs.a);
    }

    bool Color::operator<(const Color &rhs) const
    {
        if(r < rhs.r)
            return true;
        else if (r > rhs.r)
            return false;
        else if(g < rhs.g)
            return true;
        else if (g > rhs.b)
            return false;
        else if(b < rhs.b)
            return true;
        else if (b > rhs.b)
            return false;
        else return (a < rhs.a);
    }

    bool Color::operator>(const Color &rhs) const
    {
        return !(rhs < *this) && !(rhs == *this);
    }


    bool QuickObj::glRender()
    {
        glPushAttrib(GL_ALL_ATTRIB_BITS);
            
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        
        for (auto&& submesh : subMeshes)
        {
            std::string texname = materialMap[submesh.materialName].textureFile;
            if (texname.length() > 0)
            {
                GLuint texid = getOrLoadTextureID(texname);
                if (texid == 0)
                {
                    
                    log << "Invalid texture, unable to render texture in QuickObj::glRender()" << log.endl;
                }
                //std::cout << submesh.materialName << " tex=" << texid << " filename=" << materialMap[submesh.materialName].textureFile << "\n";
                glBindTexture(GL_TEXTURE_2D, texid);
            }
            glBegin(GL_TRIANGLES);
            if (submesh.vertIdxs.size() != submesh.uvIdxs.size())
            {
                log << "The number of vertices does not match the number of UV coordinates." << log.endl;
                return false;
            }

            for (size_t i = 0, ic = submesh.vertIdxs.size(); i < ic; i++)
            {
                uint32_t idx = submesh.vertIdxs[i];
                uint32_t uvidx = submesh.uvIdxs[i];
                if (idx == 0 || uvidx == 0)
                {
                    log << "Vert/UV index of 0 is invalid for OBJ." << log.endl;
                    return false;
                }
                glTexCoord2f(uvs[uvidx].x, uvs[uvidx].y);
                glVertex3f(verts[idx].x, verts[idx].y, verts[idx].z);
            }
            glEnd();
        }
        
        glPopAttrib();
        return true;
    }

#define FOURCC_DXT1 0x31545844 // Equivalent to "DXT1" in ASCII
#define FOURCC_DXT3 0x33545844 // Equivalent to "DXT3" in ASCII
#define FOURCC_DXT5 0x35545844 // Equivalent to "DXT5" in ASCII

    GLuint QuickObj::getOrLoadDDSTextureID(const std::string &texname)
    {
        //DDS Reader Borrowed from http://www.opengl-tutorial.org
        unsigned char header[124];
        FILE* fp;

        /* try to open the file */
        fp = fopen(texname.c_str(), "rb");
        if (fp == NULL)
        {
            log << "Unable to open " << texname << log.endl;
            return 0;
        }

        /* verify the type of file */
        char filecode[4];
        fread(filecode, 1, 4, fp);
        if (strncmp(filecode, "DDS ", 4) != 0)
        {
            log << "Not a dds file " << texname << log.endl;
            fclose(fp);
            return 0;
        }

        /* get the surface desc */
        fread(&header, 124, 1, fp);

        unsigned int height = *(unsigned int*)&(header[8]);
        unsigned int width = *(unsigned int*)&(header[12]);
        unsigned int linearSize = *(unsigned int*)&(header[16]);
        unsigned int mipMapCount = *(unsigned int*)&(header[24]);
        unsigned int fourCC = *(unsigned int*)&(header[80]);
        unsigned char* buffer;
        unsigned int bufsize;
        /* how big is it going to be including all mipmaps? */
        bufsize = mipMapCount > 1 ? linearSize * 2 : linearSize;
        buffer = (unsigned char*)malloc(bufsize * sizeof(unsigned char));
        fread(buffer, 1, bufsize, fp);
        /* close the file pointer */
        fclose(fp);
        unsigned int components = (fourCC == FOURCC_DXT1) ? 3 : 4;
        unsigned int format;
        switch (fourCC)
        {
        case FOURCC_DXT1:
            format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            break;
        case FOURCC_DXT3:
            format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
            break;
        case FOURCC_DXT5:
            format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            break;
        default:
            log << "Unknown compression mode " << fourCC << log.endl;
            free(buffer);
            return 0;
        }
        // Create one OpenGL texture
        GLuint textureID;
        glGenTextures(1, &textureID);
        textures[texname] = textureID;
        // "Bind" the newly created texture : all future texture functions will modify this texture
        glBindTexture(GL_TEXTURE_2D, textureID);
        unsigned int blockSize = (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ? 8 : 16;
        unsigned int offset = 0;

        /* load the mipmaps */
        for (unsigned int level = 0; level < mipMapCount && (width || height); ++level)
        {
            unsigned int size = ((width + 3) / 4) * ((height + 3) / 4) * blockSize;
            glCompressedTexImage2D(GL_TEXTURE_2D, level, format, width, height,
                                   0, size, buffer + offset);

            offset += size;
            width /= 2;
            height /= 2;
        }
        free(buffer);

        return textureID;

    }

    class TexturePixels
    {
    public:
        ccl::binary pixels;
        std::string filename;
        ip::ImageInfo info;
        ~TexturePixels()
        {
            pixels.clear();//Probably not needed, but I'm trying to track down a leak.
        }
    };

    typedef std::map<std::string, TexturePixels> texture_pixels_map_t;

    //Global texture cache:

    /***
     * Warning, this cache isn't thread safe. Nothing is done to control the lifetime
     * of the texture in the cache. To make it thread-safe, a reference count should be used
     * so texture are only removed from the cache inside a mutex, when the reference count is zero.
     * For this to work, all code that uses a cached texture  must dereference the count,
     * by calling a deref inside the cache object, so it can be protected with a mutex.
     * Why haven't I done this yet? Because it isn't needed right now.
     */
    class TextureCache
    {
        int max_textures;
        std::map<std::string, TexturePixels> images;
        std::map<std::string, int> textureAge;
        ccl::mutex mut;
        void removeOldest()
        {
            mut.lock();
            int max_age = 0;
            for(auto && age : textureAge)
            {
                max_age = std::max<int>(age.second,max_age);
            }
            for (auto && age : textureAge)
            {
                if(age.second == max_age)
                {
                    removeTexture(age.first);
                    mut.unlock();
                    return;
                }
            }
            mut.unlock();
        }

        void removeTexture(const std::string &filename)
        {
            mut.lock();
            auto iter = images.find(filename);
            if(iter!=images.end())
            {
                images.erase(iter);
            }

            auto age_iter = textureAge.find(filename);
            if (age_iter != textureAge.end())
            {
                textureAge.erase(age_iter);
            }
            mut.unlock();
        }

        void increaseAge()
        {
            mut.lock();
            for(auto &&tage : textureAge)
            {
                tage.second++;
            }
            mut.unlock();
        }

        void resetAge(const std::string &filename)
        {
            mut.lock();
            auto age_iter = textureAge.find(filename);
            if (age_iter != textureAge.end())
            {
               age_iter->second = 0;
            }
            mut.unlock();
        }
    public:
        TextureCache(int max_textures) : max_textures(max_textures)
        {
            
        }

        void store(TexturePixels image)
        {
            mut.lock();
            if (images.size() > max_textures)
            {
                removeOldest();
            }
            textureAge[image.filename] = 0;
            images[image.filename] = image;
            mut.unlock();
        }

        bool get(const std::string &textureFilename, TexturePixels &image)
        {
            mut.lock();
            auto iter = images.find(textureFilename);
            if (iter == images.end())
            {
                mut.unlock();
                return false;
            }
            increaseAge();
            resetAge(textureFilename);
            image = iter->second;
            mut.unlock();
            return true;
        }
    };

    TextureCache gTextureCache(20);


    GLuint QuickObj::getOrLoadTextureID(const std::string &texname)
    {
        //std::string texpath = ccl::joinPaths(textureDirectory,texname);
        if(textures.find(texname)!=textures.end())
            return textures[texname];
        if(ccl::stringEndsWith(texname, ".dds", false))
        {
            return getOrLoadDDSTextureID(texname);
        }

        TexturePixels tpix;
        ip::ImageInfo info;
        if(!gTextureCache.get(texname,tpix))
        {
            ccl::binary buffer;         
            if (ip::GetImagePixels(texname, info, buffer))
            {
                ip::FlipVertically(info, buffer);
                tpix.pixels = buffer;
                tpix.info = info;
                tpix.filename = texname;
                gTextureCache.store(tpix);
            }
            else
            {
                log << "Error, unable to read texture pixels for " << texname << log.endl;
                return 0;
            }
        }
        info = tpix.info;
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glEnable(GL_TEXTURE_2D);
            GLuint texid = 0;
            GLenum glerror = glGetError();
            glGenTextures(1, &texid);
            //std::cout << "texid=" << texid << " err=" << (int)glerror << "\n";
            glBindTexture(GL_TEXTURE_2D, texid);
            glerror = glGetError();
            textures[texname] = texid;
            
            glerror = glGetError();
            unsigned char *p = (unsigned char *)&tpix.pixels.at(0);

            if (info.depth == 3 && info.interleaved && info.dataType == ip::ImageInfo::UBYTE)
                glTexImage2D(GL_TEXTURE_2D, 0, 3, info.width, info.height, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
            else if (info.depth == 4 && info.interleaved && info.dataType == ip::ImageInfo::UBYTE)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
            else
                log << "Error, unknown texture format!" << log.endl;
            //if(glGenerateMipmapfunc)
            //    glGenerateMipmapfunc(GL_TEXTURE_2D);
            glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
            glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
            
            return texid;
        }

        return -1;
    }
    QuickObj &QuickObj::operator=(const QuickObj &other)
    {
        this->minX = other.minX;
        this->minY = other.minY;
        this->maxX = other.maxX;
        this->maxY = other.maxY;
        this->minZ = other.minZ;
        this->maxZ = other.maxZ;
        this->srs = other.srs;
        this->subMeshes = other.subMeshes;
        this->verts = other.verts;
        this->norms = other.norms;
        this->uvs = other.uvs;
        this->vertIdxs = other.vertIdxs;
        this->uvIdxs = other.uvIdxs;
        this->normIdxs = other.normIdxs;
        this->textureFilename = other.textureFilename;
        this->textureDirectory = other.textureDirectory;
        this->materialFilename = other.materialFilename;
        this->materialName = other.materialName;
        this->currentMaterial = other.currentMaterial;
        this->materialMap = other.materialMap;
        this->_isValid = other._isValid;
        this->textures = other.textures;
        this->objFilename = other.objFilename;
        return *this;
    }

    QuickObj::QuickObj(const QuickObj &other)
    {
        this->minX = other.minX;
        this->minY = other.minY;
        this->maxX = other.maxX;
        this->maxY = other.maxY;
        this->minZ = other.minZ;
        this->maxZ = other.maxZ;
        this->srs = other.srs;
        this->subMeshes = other.subMeshes;
        this->verts = other.verts;
        this->norms = other.norms;
        this->uvs = other.uvs;
        this->vertIdxs = other.vertIdxs;
        this->uvIdxs = other.uvIdxs;
        this->normIdxs = other.normIdxs;
        this->textureFilename = other.textureFilename;
        this->textureDirectory = other.textureDirectory;
        this->materialFilename = other.materialFilename;
        this->materialName = other.materialName;
        this->currentMaterial = other.currentMaterial;
        this->materialMap = other.materialMap;
        this->_isValid = other._isValid;
        this->textures = other.textures;
        this->objFilename = other.objFilename;
    }

    QuickObj::~QuickObj()
    {
        std::map<std::string,GLuint>::iterator iter = textures.begin();
        while(iter!=textures.end())
        {
            if(iter->second!=-1)
            {
                int texid = iter->second;                
                glDeleteTextures(1,&iter->second);
                //GLenum glerror = glGetError();
                //std::string t = iter->first;
                //std::cout << "deleted texture=" << t << " texid=" << texid << " err=" << (int)glerror << "\n";
            }
            iter++;
        }
    }

    bool QuickObj::transform(Cognitics::CoordinateSystems::EllipsoidTangentPlane *etp,
        OGRCoordinateTransformation *coordTrans,
        const sfa::Point &offset) 
    {
        for(auto& vert : verts)
        {
            double x = vert.x + offset.X();
            double y = vert.y + offset.Y();
            double z = vert.z + offset.Z();
            double local_x = 0, local_y = 0, local_z = 0;
            coordTrans->Transform(1, &x, &y, &z);
            etp->GeodeticToLocal(y,
                x,
                z,
                local_x,
                local_y,
                local_z);
            vert.x = local_x;
            vert.y = local_y;
            vert.z = local_z;
        }
        return true;
    }

    ObjCache::ObjCache(int max_objs) : max_objs(max_objs)
    {

    }

    ObjCache::~ObjCache()
    {
        mut.lock();
        for (auto && age : objAge)
        {
            delete objs[age.first];
        }
        mut.unlock();
    }

    //store takes ownership of obj and will delete it
    void ObjCache::store(QuickObj *obj)
    {
        if (!obj)
            return;
        mut.lock();
        if (objs.size() > max_objs)
        {
            removeOldest();
        }
        objAge[obj->objFilename] = 0;
        objs[obj->objFilename] = obj;
        mut.unlock();
    }

    QuickObj* ObjCache::get(const std::string &objFilename)
    {
        mut.lock();
        QuickObj* obj = NULL;
        auto iter = objs.find(objFilename);
        if (iter == objs.end())
        {
            mut.unlock();
            return NULL;
        }
        increaseAge();
        resetAge(objFilename);
        obj = iter->second;
        mut.unlock();
        return obj;
    }

    void ObjCache::removeOldest()
    {
        mut.lock();
        int max_age = 0;
        for (auto && age : objAge)
        {
            max_age = std::max<int>(age.second, max_age);
        }
        //for (auto && age : objAge)
        for(auto age_iter = objAge.begin(),end=objAge.end();age_iter!=end;age_iter++)
        {
            auto &age = *age_iter;
            if (age.second == max_age)
            {
                auto iter = objs.find(age.first);
                if (iter != objs.end())
                {
                    QuickObj *old = iter->second;
                    if (old)
                    {
                        //log << "Erasing " << old->objFilename << "..." << log.endl;
                        delete old;
                    }
                    else
                    {
                        log << old->objFilename << " doesn't have an object in the cache!" << log.endl;
                    }
                    objs.erase(iter);
                    objAge.erase(age_iter);
                }
                mut.unlock();
                return;
            }
        }
        mut.unlock();
    }

    void ObjCache::increaseAge()
    {
        mut.lock();
        for (auto &&tage : objAge)
        {
            tage.second++;
        }
        mut.unlock();
    }

    void ObjCache::resetAge(const std::string &filename)
    {
        mut.lock();
        auto iter = objAge.find(filename);
        if (iter != objAge.end())
        {
            iter->second = 0;
        }
        mut.unlock();
    }

    ObjCache gObjCache(50);
}