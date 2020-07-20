#include <string>
#ifndef DATA_CUBE_H
#define DATA_CUBE_H

class DataCube{
  public:
    std::string fileName;
    std::string dataType;
    int dimx, dimy, dimz;
    size_t num_pixels;
    float* floatArray; int* intArray; double* doubleArray; char* charArray; // Depending on the data type

    void readInData();
    void readRawFile();
    void readFitsFile();
    void readVtkFile();
    bool getDimensions(int *dims);

    bool constructedCorrectly;
    DataCube(std::string fileName);
};

#endif