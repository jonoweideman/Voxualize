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
DataCube::DataCube(void){
}

// The actuall default constructor
void DataCube::createCube(std::string fileName){
  cout << "Constructing DataCube object from " + fileName + " file." << endl;
  (*this).fileName = fileName;
  readInData();
  //generateLODModel(); To be called externally.
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

// Given the Size of the desired LOD model, create it.
float * DataCube::generateLODModel(int size_in_mb){

  // Intial LOD model is for entire cube. Set cplanes accordingly:
  current_cplanes_lod_model[0] = 0; current_cplanes_lod_model[1] = dimx-1;
  current_cplanes_lod_model[2] = 0; current_cplanes_lod_model[3] = dimy-1;
  current_cplanes_lod_model[4] = 0; current_cplanes_lod_model[5] = dimz-1;

  lod_current_size_in_mb = size_in_mb;

  int dim_min = dimx;
  if (dimy < dimx)
    dim_min = dimy;
  if (dimz < dim_min)
    dim_min = dimz;

  float array[3];
  array[0] = (float)dimx/(float)dim_min;
  array[1] = (float)dimy/(float)dim_min;
  array[2] = (float)dimz/(float)dim_min;

  float x = std::cbrt((float)size_in_mb*1000000/(array[0]*array[1]*array[2]*4));

  array[0] = array[0]*x;
  array[1] = array[1]*x;
  array[2] = array[2]*x;

  // array now contains dimensions of new cube - in bytes.

  // Initialize LOD model variables.
  new_dim_x = ceil(array[0]);
  new_dim_y = ceil(array[1]);
  new_dim_z = ceil(array[2]);

  cout << "New dimensions: " << new_dim_x << ' ' << new_dim_y<< ' ' << new_dim_z << endl;
  // current_cplanes_lod_model[0] = 0; current_cplanes_lod_model[1] = new_dim_x-1;
  // current_cplanes_lod_model[2] = 0; current_cplanes_lod_model[3] = new_dim_y-1;
  // current_cplanes_lod_model[4] = 0; current_cplanes_lod_model[5] = new_dim_z-1;

  LOD_num_pixels = new_dim_x*new_dim_y*new_dim_z;
  LOD_num_bytes = LOD_num_pixels * sizeof(float);
  cout << "LOD_num_bytes: " << LOD_num_bytes << endl;
  LODFloatArray = new float [LOD_num_pixels];

  x_scale_factor = (float)dimx / new_dim_x;
  y_scale_factor = (float)dimy / new_dim_y;
  z_scale_factor = (float)dimz / new_dim_z;

  cout << x_scale_factor << ' ' << y_scale_factor << ' ' << z_scale_factor << endl;

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

float * DataCube::generateLODModelNew(int size_in_mb, float * cropping_dims){
  cout << "Cropping_dims on backend: " << current_cplanes_lod_model[0] << ' ' << current_cplanes_lod_model[1] << ' ' << current_cplanes_lod_model[2] << endl;
  cout << current_cplanes_lod_model[3] << ' ' << current_cplanes_lod_model[4] << ' ' << current_cplanes_lod_model[5] << endl;
  if (cropping_dims[0]!=0 || cropping_dims[1]+1!=new_dim_x
      || cropping_dims[2]!=0 && cropping_dims[3]+1!=new_dim_y
      || cropping_dims[4]!=0 && cropping_dims[5]+1!=new_dim_z){
    // New ROI.
    cout << "New ROI requested" << endl;
    cout << "Cropping_dims requested: " << cropping_dims[0] << ' ' << cropping_dims[1] << ' ' << cropping_dims[2] << endl;
    cout << cropping_dims[3] << ' ' << cropping_dims[4] << ' ' << cropping_dims[5] << endl;
    current_cplanes_lod_model[0] = ceil(cropping_dims[0]*x_scale_factor);
    current_cplanes_lod_model[1] = ceil(cropping_dims[1]*x_scale_factor);
    current_cplanes_lod_model[2] = ceil(cropping_dims[2]*y_scale_factor);
    current_cplanes_lod_model[3] = ceil(cropping_dims[3]*y_scale_factor);
    current_cplanes_lod_model[4] = ceil(cropping_dims[4]*z_scale_factor);
    current_cplanes_lod_model[5] = ceil(cropping_dims[5]*z_scale_factor);
  } else if (size_in_mb == lod_current_size_in_mb) {
    cout << "Same cropping plans and size in mb detected. Hence no new cube will be generated." << endl;
    return &LODFloatArray[0];
  }

  // Generate a LOD model with same cplanes, but new mem size.
  int largest_possible_model = (current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0])*
                               (current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2])*
                               (current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4])
                               *4;
  
  int use_size_in_mb;
  if (size_in_mb*1000000 >= largest_possible_model){
    cout << "The requested cube size is larger than the largest possible cube given the cropping region." << endl;
    cout << "Hence a size of " << largest_possible_model << " bytes will be used." << endl;
    // Now must just set LOD model to be the exact same as cropping region.
    // No need to reduce/take means, etc.

    // Initialize LOD model variables.
    new_dim_x = current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0];
    new_dim_y = current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2];
    new_dim_z = current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4];

    x_scale_factor = 1;
    y_scale_factor = 1;
    z_scale_factor = 1;

    delete [] LODFloatArray;
    LOD_num_pixels = (current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0])*
                     (current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2])*
                     (current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4]);
    LODFloatArray = new float [LOD_num_pixels];
    LOD_num_bytes = LOD_num_pixels*4;
    
    cout << current_cplanes_lod_model[0] << ' ' << current_cplanes_lod_model[2] << ' ' << current_cplanes_lod_model[4] << endl;
    cout << new_dim_x << ' ' << new_dim_y << ' ' << new_dim_z << endl;
    for (int i = 0, x = current_cplanes_lod_model[0]; i < new_dim_x; i++, x++){
      for (int j = 0, y = current_cplanes_lod_model[2]; j < new_dim_y; j++, y++){
        for (int k = 0, z = current_cplanes_lod_model[4]; k < new_dim_z; k++, z++){
          *(LODFloatArray + i + j*new_dim_x + k*new_dim_x*new_dim_y) = *(floatArray + x + y*dimx + z*dimx*dimy);
        }
      }
    }
    cout << "Finished generating largest LOD model" << endl;
    return &LODFloatArray[0];
    
  } else {
    // Okay, the requested size is possible, and is differnet to current.
    cout << "The requested size is possible. Generating..." << endl;
    int dim_min = current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0];
    if (current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2] < current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0])
      dim_min = current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2];
    if (current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4] < dim_min)
      dim_min = current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4];

    float array[3];
    array[0] = (float)(current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0])/(float)dim_min;
    array[1] = (float)(current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2])/(float)dim_min;
    array[2] = (float)(current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4])/(float)dim_min;

    float x = std::cbrt((float)size_in_mb*1000000/(array[0]*array[1]*array[2]*4));

    array[0] = array[0]*x;
    array[1] = array[1]*x;
    array[2] = array[2]*x;

    // array now contains dimensions of new cube - in bytes.

    // Initialize LOD model variables.
    new_dim_x = ceil(array[0]);
    new_dim_y = ceil(array[1]);
    new_dim_z = ceil(array[2]);

    x_scale_factor = (float)(current_cplanes_lod_model[1]+1-current_cplanes_lod_model[0]) / new_dim_x;
    y_scale_factor = (float)(current_cplanes_lod_model[3]+1-current_cplanes_lod_model[2]) / new_dim_y;
    z_scale_factor = (float)(current_cplanes_lod_model[5]+1-current_cplanes_lod_model[4]) / new_dim_z;

    delete [] LODFloatArray;
    LOD_num_pixels = new_dim_x*new_dim_y*new_dim_z;
    LOD_num_bytes = LOD_num_pixels*4;
    LODFloatArray = new float [LOD_num_pixels];

    // Now: need to sample a LOD cube of these dimensions from the cropping region (which is the same).
    for (int i=0; i<new_dim_x; i++){
      for (int j=0; j<new_dim_y; j++){
        for (int k=0; k<new_dim_z; k++){
          // Calc mean/min/max/etc...
          *(LODFloatArray + i + j*new_dim_x + k*new_dim_x*new_dim_y) = calculateMax(i,j,k, &current_cplanes_lod_model[0]);
          //*(LODFloatArray + i + j*new_dim_x + k*new_dim_x*new_dim_y) = calculateMean(i,j,k);
        }
      }
    }
    cout << "Finished generating new LOD model." << endl;
    return &LODFloatArray[0];
  }
}

float DataCube::calculateMax(int i, int j, int k){
  float max_pixel = numeric_limits<float>::min();
  for (int x = floor(i*x_scale_factor); x < ceil((i+1)*x_scale_factor) && x < dimx; x++){
    for (int y = floor(j*y_scale_factor); y < ceil((j+1)*y_scale_factor) && y < dimy; y++){
      for (int z = floor(k*z_scale_factor); z < ceil((k+1)*z_scale_factor) && z < dimz; z++){
        float temp = *(floatArray + x + y*dimx + z*dimx*dimy);
        if (isfinite(temp) && temp > max_pixel){
          max_pixel = temp;
        }
      }
    }
  }
  return max_pixel;
}

float DataCube::calculateMax(int i, int j, int k, int * cplanes){
  float max_pixel = numeric_limits<float>::min();
  for (int x = floor((float)cplanes[0]+i*x_scale_factor); x < ceil((float)cplanes[0]+(i+1)*x_scale_factor) && x < cplanes[1]; x++){
    for (int y = floor((float)cplanes[2]+j*y_scale_factor); y < ceil((float)cplanes[2]+(j+1)*y_scale_factor) && y < cplanes[3]; y++){
      for (int z = floor((float)cplanes[4]+k*z_scale_factor); z < ceil((float)cplanes[4]+(k+1)*z_scale_factor) && z < cplanes[5]; z++){
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
  for (int x = ceil(i*x_scale_factor); x < ceil((i+1)*x_scale_factor) && x < dimx; x++){
    for (int y = ceil(j*y_scale_factor); y < ceil((j+1)*y_scale_factor) && y < dimy; y++){
      for (int z = ceil(k*z_scale_factor); z < ceil((k+1)*z_scale_factor) && z < dimz; z++){
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

float * DataCube::getFloatPointerFullModel(){
  return floatArray;
}