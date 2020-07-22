#include <string>
#ifndef DATA_CUBE_H
#define DATA_CUBE_H

class DataCube{
  public:
    std::string fileName;
    std::string dataType;
    int dimx, dimy, dimz;
    size_t num_pixels;
    size_t num_bytes;

    // This should be templated. 
    float *floatArray; int* intArray; double* doubleArray; char* charArray; // Depending on the data type

    // The LOD related variables:
    float *LODFloatArray;
    int x_scale_factor = 2; int y_scale_factor = 2; int z_scale_factor = 2;
    int new_dim_x; int new_dim_y; int new_dim_z;
    size_t LOD_num_pixels;
    size_t LOD_num_bytes;

    bool constructedCorrectly;
    DataCube(std::string fileName);
    ~DataCube();
    void readInData();
    void readRawFile();
    void readFitsFile();
    void readVtkFile();
    bool getDimensions(int *dims);
    float calculateMax(int i, int j, int k);
    float calculateMean(int i, int j, int k);

    float * generateLODModel();
    char * getBytePointerFullModel();
    char * getBytePointerLODModel();

};

#endif