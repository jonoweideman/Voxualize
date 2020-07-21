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
    float* floatArray; int* intArray; double* doubleArray; char* charArray; // Depending on the data type

    bool constructedCorrectly;
    DataCube(std::string fileName);
    void readInData();
    void readRawFile();
    void readFitsFile();
    void readVtkFile();
    bool getDimensions(int *dims);

    char * getBytePointer();

};

#endif