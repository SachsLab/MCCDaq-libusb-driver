//
//  mccdevice.h
//  This is a modified version of the libusb-enabled C++ driver found
//  on the MeasurementComputing site: http://kb.mccdaq.com/KnowledgebaseArticle50047.aspx
//  I have modified it to only do synchronous polling and to allow for variable packet sizes.
//
//  Created by Chadwick Boulay on 2014-03-12.
//
//

#ifndef ____mccdevice__
#define ____mccdevice__

#include <iostream>
#include <libusb.h> //includes typedef fixes, so maybe below is not necessary.
#include <string>
#include <sstream>
#include <exception>

/*
 #ifdef _MSC_VER
 #if _MSC_VER >= 1600
 #include <cstdint>
 #else
 typedef __int8              int8_t;
 typedef __int16             int16_t;
 typedef __int32             int32_t;
 typedef __int64             int64_t;
 typedef unsigned __int8     uint8_t;
 typedef unsigned __int16    uint16_t;
 typedef unsigned __int32    uint32_t;
 typedef unsigned __int64    uint64_t;
 #endif
 #elif __GNUC__ >= 3
 #include <cstdint>
 #endif
	*/

///////////
//Constants
///////////
#define STRINGMESSAGE 0x80
#define FPGADATAREQUEST 0x51
#define RAWDATA 0x81
#define MAX_MESSAGE_LENGTH 64
#define MCC_VENDOR_ID 0x09db
//Device Product IDs
#define USB_2001_TC 0x00F9
#define USB_7202 0x00F2
#define USB_7204 0x00F0
#define USB_1608_GX 0x0111
#define USB_1608_GX_2AO 0x0112
#define USB_1608_FS_PLUS 0x00EA
//#define FIRMWAREPATH "/usr/lib/daqflex/"
#define SLOPE 0
#define OFFSET 1
#define FIRSTHALF true
#define SECONDHALF false

/////////////
//Error Codes
/////////////
enum mcc_err{
    MCC_ERR_NO_DEVICE,
    MCC_ERR_INVALID_ID,
    MCC_ERR_USB_INIT,
    MCC_ERR_PIPE,
    MCC_ERR_LIBUSB_TIMEOUT,
    MCC_ERR_TRANSFER_FAILED,
    MCC_ERR_LIBUSB_TRANSFER_STALL,
    MCC_ERR_LIBUSB_TRANSFER_OVERFLOW,
    MCC_ERR_UNKNOWN_LIB_USB_ERR,
    MCC_ERR_INVALID_BUFFER_SIZE,
    MCC_ERR_CANT_OPEN_FPGA_FILE,
    MCC_ERR_FPGA_UPLOAD_FAILED,
    MCC_ERR_ACCESS,
};

using namespace std;

//////////////////
//Static functions
//////////////////

//Convert a libusb error code into an mcc_err
static mcc_err libUSBError(int err)
{
    switch(err)
    {
        case LIBUSB_ERROR_TIMEOUT:
            return MCC_ERR_LIBUSB_TIMEOUT;
        case LIBUSB_ERROR_PIPE:
            return MCC_ERR_PIPE;
        case LIBUSB_ERROR_NO_DEVICE:
            return MCC_ERR_NO_DEVICE;
        default:
            return MCC_ERR_UNKNOWN_LIB_USB_ERR;
    }
};

//Convert an mcc_err int to a human readable string
static string errorString(int err)
{
    stringstream unknownerror;
    
    switch(err)
    {
        case MCC_ERR_ACCESS:
            return "Insufficient USB permisions\n";
        case MCC_ERR_NO_DEVICE:
            return "No Matching Device Found\n";
        case MCC_ERR_INVALID_ID:
            return "Invalid Device ID\n";
        case MCC_ERR_USB_INIT:
            return "Failed to Init USB\n";
        case MCC_ERR_PIPE:
            return "Libusb Pipe Error, possibly invalid command\n";
        case MCC_ERR_LIBUSB_TIMEOUT:
            return "Transfer Timed Out\n";
        case MCC_ERR_UNKNOWN_LIB_USB_ERR:
            return "Unknown LibUSB Error\n";
        case MCC_ERR_INVALID_BUFFER_SIZE:
            return "Buffer must be and integer multiple of 32\n";
        case MCC_ERR_CANT_OPEN_FPGA_FILE:
            return "Cannot open FPGA file\n";
        case MCC_ERR_FPGA_UPLOAD_FAILED:
            return "FPGA firmware could not be uploaded\n";
        default:
            unknownerror << "Error number " << err << " has no text\n";
            return unknownerror.str();
    }
};

static string toNameString(int idProduct)
{
    switch(idProduct)
    {
        case USB_2001_TC:
            return "USB-2001-TC";
        case USB_7202:
            return "USB-7202";
        case USB_7204:
            return "USB-7204";
        case USB_1608_GX:
            return "USB-1608GX";
        case USB_1608_GX_2AO:
            return "USB-1608GX-2AO";
        case USB_1608_FS_PLUS:
            return "USB-1608-FS-PLUS";
        default:
            return "Invalid Product ID";
    }
};

//Is the specified product ID is an MCC product ID? Called when initializing.
static bool isMCCProduct(int idProduct)
{
    switch(idProduct)
    {
        case USB_2001_TC: case USB_7202: case USB_7204:
        case USB_1608_FS_PLUS: case USB_1608_GX: case USB_1608_GX_2AO://same for all products
            return true;
        default:
            return false;
            break;
    }
};

/////////
//Classes
/////////

class intTransferInfo
{
public:
    intTransferInfo(){};
    unsigned short* dataptr;
};

//converts a stringstream to a numeric value
template<class T>
T fromString(const std::string& s)
{
    std::istringstream stream (s);
    T t;
    stream >> t;
    return t;
}

//TODO: Make setters and getters for chans, rate, range, etc.
class MCCDevice
{
public:
    MCCDevice(int idProduct);
    MCCDevice(int idProduct, string mfgSerialNumber);
    ~MCCDevice();
    
    string sendMessage(string message);
    void flushInputData();
    void readScanData(unsigned short* data, int length);//, int rate);
    void getBlock();
    void reconfigure(); //Called during initialization, should be called again after any settings are changed.
    float scaleAndCalibrateData(unsigned short data, int chanIdx);
    //static short calData(unsigned short data, int slope, int offset);//?
    float sampRate;
    unsigned short* mData;
    int mSamplesPerBlock;
    
private:
    //variables set during class instantiation
    int idProduct;
    libusb_device ** list; //This is a member variable because it is used during destruction too.
    libusb_device_handle* dev_handle;
    unsigned short maxCounts;
    //Variables set by getScanParams (libusb_control_transfer of LIBUSB_REQUEST_GET_DESCRIPTOR)
    unsigned char endpoint_in;
    unsigned char endpoint_out;
    unsigned short bulkPacketSize;
    //Variables set by reconfigure
    float *calSlope;
    float *calOffset;
    int *minVoltage;
    int *maxVoltage;
    int mChannelCount;
    
    /*
     struct limit {
     int lowChan;
     int highChan;
     int32_t maxScanRate;
     int32_t maxScanThruput;
     } myLimits;
     */
    
    //libusb_transfer* transfer;//?
    //intTransferInfo* transferInfo;//?
    
    // Methods
    void initDevice(int idProduct, string mfgSerialNumber);//Called by constructors.
    void getScanParams(); //Called during initialization. sets endpoint_in, endpoint_out, bulkPacketSize
    //void getLimits(); //Called during initialization. Gets chan range, scan rate, etc.
    void sendControlTransfer(string message);//Called by sendMessage
    string getControlTransfer();//Called by sendMessage
    
    //static unsigned int getNumRanges();//?
    
    //Static methods to operate on libusb returns.
    static unsigned char getEndpointInAddress(unsigned char* data, int data_length);//called by getScanParams
    static unsigned char getEndpointOutAddress(unsigned char* data, int data_length);//called by getScanParams
    static unsigned short getBulkPacketSize(unsigned char* data, int data_length);//called by getScanParams
};

#endif /* defined(____mccdevice__) */
