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
    float x_scale_factor; float y_scale_factor; float z_scale_factor;
    int new_dim_x; int new_dim_y; int new_dim_z;
    size_t LOD_num_pixels;
    size_t LOD_num_bytes;

    bool constructedCorrectly;
    DataCube(void);
    void createCube(std::string fileName);
    ~DataCube();
    void readInData();
    void readRawFile();
    void readFitsFile();
    void readVtkFile();
    bool getDimensions(int *dims);
    float calculateMax(int i, int j, int k);
    float calculateMean(int i, int j, int k);

    float * generateLODModel(int size_in_bytes);
    char * getBytePointerFullModel();
    char * getBytePointerLODModel();
    float * getFloatPointerFullModel();

};

#endif