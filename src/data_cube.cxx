#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <math.h>
#include <limits>
#include "data_cube.h"

using namespace std;
// "Default" constructor
DataCube::DataCube(std::string fileName){
  cout << "Constructing DataCube object from " + fileName + " file." << endl;
  (*this).fileName = fileName;
  readInData();
  generateLODModel();
}

// Default destructor
DataCube::~DataCube(){
  delete [] floatArray;
  delete [] LODFloatArray;
}

void DataCube::readInData(){
  if (fileName.find('.') > -1){
    string fileSuffix(fileName.substr(fileName.find('.')));
    if (fileSuffix == ".fits"){
      //Read FITS file. Don't need to search in File_Information.txt
      readFitsFile();
    } else if (fileSuffix == ".arr") {
      // Read raw file. Need to search in File_information.txt
      readRawFile();
    } else if (fileSuffix == ".vtk") {
      // Read vtk. I assume it has dimensions in the file.
      readVtkFile();
    } else {
      // Undetected file extension, attempt to read it as raw...
      readRawFile();
    } 
  } else {
    // No file extension. Attempt raw read in
    readRawFile();
  }
}

void DataCube::readRawFile(){
  // Need dimensions
  int dims[3];
  if (getDimensions(&dims[0])){
    dimx = dims[0]; dimy = dims[1]; dimz = dims[2];
    num_pixels = dimx * dimy * dimz;
    num_bytes = num_pixels * sizeof(float);
    if (dataType == "float"){ 
      floatArray = new float[num_pixels];
      ifstream input_file("../../../Data/" + fileName, ios::binary);
      input_file.read((char *)floatArray, num_pixels * sizeof(float));
      constructedCorrectly = true;
      return;
    }
    else if (dataType == "int"){
      intArray = new int[num_pixels];
      ifstream input_file("../../../Data/" + fileName, ios::binary);
      input_file.read((char *)intArray, num_pixels * sizeof(int));
      constructedCorrectly = true;
      return;
    }
    else { 
      cerr<< "Unrecognized data type." << endl;
      constructedCorrectly = false;
      return;
    }


  } else {
    std::cerr<< "Unrecognized file type or is raw input and dimensions not specified in Data/File_Information" << std::endl;
    constructedCorrectly = false;
  }
}

void DataCube::readFitsFile(){
  // Do not need dimensions
  return;
}

void DataCube::readVtkFile(){
  // Do not need dimensions
  return;
}

bool DataCube::getDimensions(int *dims){
  // If filename is in File_Information.txt, return true.
  ifstream infile("../../../Data/File_Information.txt");
  string line;

  while (getline(infile, line)){
    if (line[0] == '#') {continue;} // Line is a comment

    istringstream iss(line);
    string tempFileName;
    string tempDataType;
    
    iss >> tempFileName;
    iss >> tempDataType;
    iss >> *dims; iss >> *(dims+1); iss >> *(dims+2);

    if (tempFileName == fileName){
      // Found the file.
      cout << "Found the file in File_Information.txt" << endl;
      dataType = tempDataType;
      return true;
    } else {
      continue;
    }
  }
  return false; // Could not find file.
}

float * DataCube::generateLODModel(){
  // Initialize LOD model variables.
  new_dim_x = ceil((float)dimx/(float)x_scale_factor);
  new_dim_y = ceil((float)dimy/(float)y_scale_factor);
  new_dim_z = ceil((float)dimz/(float)z_scale_factor);
  LOD_num_pixels = new_dim_x*new_dim_y*new_dim_z;
  LOD_num_bytes = LOD_num_pixels * sizeof(float);
  LODFloatArray = new float [LOD_num_pixels];

  for (int i=0; i<new_dim_x; i++){
    for (int j=0; j<new_dim_y; j++){
      for (int k=0; k<new_dim_z; k++){
        // Calc mean/min/max/etc...
        *(LODFloatArray + i + j*new_dim_x + k*new_dim_x*new_dim_y) = calculateMax(i,j,k);
        //*(LODFloatArray + i + j*new_dim_x + k*new_dim_x*new_dim_y) = calculateMean(i,j,k);
      }
    }
  }
  return LODFloatArray;
}

float DataCube::calculateMax(int i, int j, int k){
  float max_pixel = numeric_limits<float>::min();
  for (int x = i*x_scale_factor; x < (i+1)*x_scale_factor && x < dimx; x++){
    for (int y = j*y_scale_factor; y < (j+1)*y_scale_factor && y < dimy; y++){
      for (int z = k*z_scale_factor; z < (k+1)*z_scale_factor && z < dimz; z++){
        float temp = *(floatArray + x + y*dimx + z*dimx*dimy);
        if (isfinite(temp) && temp > max_pixel){
          max_pixel = temp;
        }
      }
    }
  }
  return max_pixel;
}

float DataCube::calculateMean(int i, int j, int k){
  float sum_pixels = 0;
  int temp_num_pixels = 0;
  for (int x = i*x_scale_factor; x < (i+1)*x_scale_factor && x < dimx; x++){
    for (int y = j*y_scale_factor; y < (j+1)*y_scale_factor && y < dimy; y++){
      for (int z = k*z_scale_factor; z < (k+1)*z_scale_factor && z < dimz; z++){
        float temp = *(floatArray + x + y*dimx + z*dimx*dimy);
        if (isfinite(temp)){
          sum_pixels += temp;
          temp_num_pixels++;
        }
      }
    }
  }
  return sum_pixels / temp_num_pixels;
}

char * DataCube::getBytePointerFullModel(){
  return reinterpret_cast<char *>(floatArray);
}

char * DataCube::getBytePointerLODModel(){
  return reinterpret_cast<char *>(LODFloatArray);
}