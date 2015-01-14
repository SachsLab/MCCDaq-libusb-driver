#ifndef MCCDEVICE_H
#define MCCDEVICE_H

#include <iostream>
#include "libusb.h"
#include <string>
#include <sstream>
#include <exception>

//typedef void (*mcc_cb_fn)(unsigned short* data, int transferred);
// ERROR CODES
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
};

/////////////////////////
// Statics & Constants //
/////////////////////////
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
#define USB_1608_FS_PLUS 0x00EA
#define FIRMWAREPATH "/usr/lib/daqflex/"
#define SLOPE 0
#define OFFSET 1
#define FIRSTHALF true
#define SECONDHALF false

using namespace std;

//////////////////////
// Static Functions //
//////////////////////

//Convert a libusb error code into an mcc_err
static mcc_err libUSBError(int err)  // UNUSED!?
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

//Convert an mcc_err int to a human readable string. UNUSED!?
static string errorString(int err)
{
    stringstream unknownerror;
    switch(err)
    {
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

static string toNameString(int idProduct)  //UNUSED!?
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
        case USB_1608_FS_PLUS:
            return "USB-1608-FS-PLUS";
        default:
            return "Invalid Product ID";
    }
};

//Is the specified product ID is an MCC product ID?
static bool isMCCProduct(int idProduct) //UNUSED!?
{
    switch(idProduct)
    {
        case USB_2001_TC:
        case USB_7202:
        case USB_7204:
        case USB_1608_GX:
        case USB_1608_FS_PLUS:
            return true;  //same for all products
        default:
            return false;
            break;
    }
};


/////////////
// Classes //
/////////////

class intTransferInfo
{
public:
    intTransferInfo(){};
    unsigned short* dataptr;
};

template<class T>
T fromString(const std::string& s)  // converts a stringstream to a numeric value
{
    std::istringstream stream (s);
    T t;
    stream >> t;
    return t;
}

class MCCDevice
{
public:
    // Properties
    struct scanPar_t {
        float sampleRate;
        int lowChan;
        int highChan;
        int channelCount;
        int range;  // TODO: enum
        int samplesPerBlock;
    } mScanParams;
    
    // Internally maintained data buffer.
    struct dataBuff_t {
        unsigned short* data;
        unsigned int currIndex;
        unsigned int currCount;
    } mDataBuffer;
    
    // Methods
    // Constructors and Destructors
    MCCDevice(int idProduct);
    MCCDevice(int idProduct, string mfgSerialNumber);
    ~MCCDevice();
    // Device communication
    string sendMessage(string message);  // send message to device. Where can we find a list of valid messages?
    void readScanData(unsigned short* data, int length);
    void getBlock();  // Wrapper around readScanData.
    void flushInputData();  // Read data off device until there are no data left to be read.
    void getScanParams();  // Call to get channelCount, sampleRate, and gain+offset+range for each channel.
    float scaleAndCalibrateData(unsigned short data, int chanIdx);  // Calibrate data.
    
private:
    // Properties
    unsigned char mEndpointIn;
    unsigned char mEndpointOut;
    unsigned short mBulkPacketSize;
    libusb_device_handle* pDevHandle;
    libusb_device ** pDevList;
    int mIdProduct;  // Will match one of the device DEFINE above.
    
    unsigned short mMaxCounts;  // has to do with nBits of A2D, I think.
    float *pCalSlope;    // Gain for calibration - per channel
    float *pCalOffset;   // Offset for calibration - per channel
    int *pMinVoltage;    // Range minimum - per channel
    int *pMaxVoltage;    // Range maximum - per channel
    
    // Methods
    void initDevice(int idProduct, string mfgSerialNumber); // Called by constructors.
    void sendControlTransfer(string message);  // Actually send message to device.
    string getControlTransfer();
    void getEndpoints();
};

#endif // MCCDEVICE
