#include <iostream>
#include <fstream>
#include <string>
#include <ctype.h>
#include "libusb.h"

#include "mccdevice.h"

using namespace std;

//This method finds the first available device where product ID == idProduct
MCCDevice::MCCDevice(int idProduct)
{
    string mfgSerialNumber = "NULL";
    initDevice(idProduct, mfgSerialNumber);
}

//This method finds a unique device where product ID == idProduct and serial number == mfgSerialNumber
MCCDevice::MCCDevice(int idProduct, string mfgSerialNumber)
{
    initDevice(idProduct, mfgSerialNumber);
}

MCCDevice::~MCCDevice () {
    //Free memory and devices
    libusb_release_interface(pDevHandle, 0);
    libusb_close(pDevHandle);
    libusb_free_device_list(pDevList, true);
    libusb_exit(NULL);
    pCalSlope = nullptr;
    pCalOffset = nullptr;
    pMinVoltage = nullptr;
    pMaxVoltage = nullptr;
    delete mDataBuffer.data;
    mDataBuffer.data = nullptr;
}

//Find the device, open it, claim it.
//Set idProduct, maxCounts, pDevList, pDevHandle.
//This function is called by the constructor functions.
void MCCDevice::initDevice(int idProduct, string mfgSerialNumber)
{
    int i;
    bool found = false;
    ssize_t sizeOfList;
    libusb_device_descriptor desc;
    libusb_device* device;
    string mfgsermsg = "?DEV:MFGSER";
    string retMessage;
    stringstream firmwarefile;
    string response;
    
    //Check if the product ID is a valid MCC product ID
    if(!isMCCProduct(idProduct))
        throw MCC_ERR_INVALID_ID;
    
    //Initialize USB libraries
    if(libusb_init(NULL) != 0)
    {
        throw MCC_ERR_USB_INIT;
    }
    
    //Get the list of USB devices connected to the PC
    sizeOfList = libusb_get_device_list(NULL, &pDevList);
    
    //Traverse the list of USB devices to find the requested device
    for(i=0; (i<sizeOfList) && (!found); i++)
    {
        device = pDevList[i];
        libusb_get_device_descriptor(device, &desc);
        if(desc.idVendor == MCC_VENDOR_ID && desc.idProduct == idProduct)
        {
            
            //cout << "Found " << toNameString(idProduct) << "\n";
            
            //Open the device
            if(!libusb_open(device, &pDevHandle))
            {
                //Claim interface with the device
                if(!libusb_claim_interface(pDevHandle,0))
                {
                    getEndpoints();  // Get endpoint_in, endpoint_out, bulkPacketSize
                    try{
                        retMessage = sendMessage(mfgsermsg);  // get the device serial number
                    }
                    catch(mcc_err err){
                        throw err;
                    }
                    retMessage.erase(0,11);  // Erase message while keeping serial number
                    //cout << "Found " << toNameString(idProduct) << " with Serial Number " << retMessage << "\n";
                    
                    //if mfgSerialNumber was passed in, check that the serial numbers are the same
                    if (mfgSerialNumber.compare("NULL")!=0 && retMessage.compare(mfgSerialNumber)!=0)
                        libusb_release_interface(pDevHandle, 0);  // serial numbers are not the same, release device and continue on
                    else
                        found = true;  //serial numbers are the same or we take any (NULL)
                }
            }
        }
    }
    
    if(!found)
    {
        throw MCC_ERR_NO_DEVICE;
    }
    else
    {
        mIdProduct = idProduct;
        mMaxCounts = 0xFFFF;  // So far there are no supported devices that have a different maxCounts.
        getScanParams();
    }
}


// Run this after changing settings.
// Gets channelCount, sampleRate, gain+offset+range for each channel, and resets data buffer.
void MCCDevice::getScanParams()
{
    string respLow, respHigh, respOff, respSlope, respRange, respRate;
    stringstream strOff, strSlope, strRange;
    respLow = sendMessage("?AISCAN:LOWCHAN");
    mScanParams.lowChan = fromString<int>(respLow.erase(0, 15));
    respHigh = sendMessage("?AISCAN:HIGHCHAN");
    mScanParams.highChan = fromString<int>(respHigh.erase(0, 16));
    mScanParams.channelCount = mScanParams.highChan - mScanParams.lowChan + 1;
    respRate = sendMessage("?AISCAN:RATE");
    mScanParams.sampleRate = fromString<float>(respRate.erase(0, 12));
    
    //stringstream strBuff;
    //strBuff << "AISCAN:BUFSIZE=" << mChannelCount*mSamplesPerBlock*2;
    //sendMessage(strBuff.str());
    //sendMessage("AISCAN:BUFOVERWRITE=DISABLE");
    
    //Reset members that are per-channel arrays.
    pCalSlope = nullptr; pCalSlope = new float[mScanParams.channelCount];
    pCalOffset = nullptr; pCalOffset = new float[mScanParams.channelCount];
    pMinVoltage = nullptr; pMinVoltage = new int[mScanParams.channelCount];
    pMaxVoltage = nullptr; pMaxVoltage = new int[mScanParams.channelCount];
    
    for (int chanIdx = mScanParams.lowChan; chanIdx<=mScanParams.highChan; chanIdx++)
    {
        //Reset string streams.
        strOff.str("");
        strSlope.str("");
        strRange.str("");
        
        //strSlope = string("?AI{") + "}:SLOPE";
        strSlope << "?AI{" << chanIdx << "}:SLOPE";
        respSlope = sendMessage(strSlope.str());
        pCalSlope[chanIdx - mScanParams.lowChan] = fromString<float>(respSlope.erase(0,12));
        
        strOff << "?AI{" << chanIdx << "}:OFFSET";
        respOff = sendMessage(strOff.str());
        pCalOffset[chanIdx - mScanParams.lowChan] = fromString<float>(respOff.erase(0,13));
        //        calOffset[chanIdx-mScanParams.lowChan] = atof(respSlope.erase(0, 13).c_str());
        
        strRange << "?AI{" << chanIdx << "}:RANGE";
        respRange = sendMessage(strRange.str());
        respRange = respRange.erase(0, 12);
        if (respRange == "BIP10V"){
            pMinVoltage[chanIdx - mScanParams.lowChan] = -10;
            pMaxVoltage[chanIdx - mScanParams.lowChan] = 10;
        }else if (respRange == "BIP5V"){
            pMinVoltage[chanIdx - mScanParams.lowChan] = -5;
            pMaxVoltage[chanIdx - mScanParams.lowChan] = 5;
        }else if (respRange == "BIP2V"){
            pMinVoltage[chanIdx - mScanParams.lowChan] = -2;
            pMaxVoltage[chanIdx - mScanParams.lowChan] = 2;
        }else if (respRange == "BIP1V"){
            pMinVoltage[chanIdx - mScanParams.lowChan] = -1;
            pMaxVoltage[chanIdx - mScanParams.lowChan] = 1;
        }
        //cout << "Channel " << chanIdx << " Slope: " << pCalSlope[chanIdx-lowChan] << " Offset: " << pCalOffset[chanIdx-mScanParams.lowChan] << " in Range " << minVoltage[chanIdx-mScanParams.lowChan] << ":" << maxVoltage[chanIdx-mScanParams.lowChan] << "\n\n";
        
        mDataBuffer.data = new unsigned short[mScanParams.samplesPerBlock * mScanParams.channelCount];
    }
}

//Will return at most a 64 character array.
//Returns response if transfer successful, null if not
string MCCDevice::sendMessage(string message)
{
    try{
        sendControlTransfer(message);
        return getControlTransfer();
    }
    catch(mcc_err err)
    {
        throw err;
    }
}

//Send a message to the device. No error checking.
void MCCDevice::sendControlTransfer(string message)
{
    //StringUtil::toUpper(message);
    //cout << "Sending: " << message << "\n";
    int numBytesTransferred;
    
    //Convert message to data (char array MAX_MESSAGE_LENGTH (64) long_.
    uint16_t length = message.length();
    const char* msgData = message.data();
    unsigned char data[MAX_MESSAGE_LENGTH];
    for (uint16_t i = 0; i < MAX_MESSAGE_LENGTH; i++) {
        data[i] = (i < length) ? msgData[i] : 0;  // Pad with zeros if necessary.
    }
    numBytesTransferred = libusb_control_transfer(pDevHandle, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_ENDPOINT_OUT,
                                                  STRINGMESSAGE, 0, 0, data, MAX_MESSAGE_LENGTH, 1000);
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
}

//Receive a message from the device. This should follow a call to sendControlTransfer.
//It will return a pointer to a at most 64 character array.
string MCCDevice::getControlTransfer()
{
    int messageLength;
    unsigned char message[64];
    string strmessage;
    messageLength = libusb_control_transfer(pDevHandle,  LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_ENDPOINT_IN,
                                            STRINGMESSAGE, 0, 0, message, 64, 1000);
    if(messageLength < 0)
        throw libUSBError(messageLength);
    strmessage = (char*)message;
    //cout << "Got: " << strmessage << "\n\n";
    return strmessage;  // e.g. DEV:MFGSER=018FF921 in response to ?DEV:MFGSER
}

//Read analog in scan data. length is max bytes to transfer.
void MCCDevice::readScanData(unsigned short* data, int length)
{
    int err = 0, totalTransferred = 0, transferred;
    unsigned char* dataAsByte = (unsigned char*)data;
    unsigned int timeout = 2000000;  //TODO: /(bulckPacketSize*rate)
    
    do{
        err =  libusb_bulk_transfer(pDevHandle, mEndpointIn, &dataAsByte[totalTransferred],
                                    mBulkPacketSize, &transferred, timeout);
        totalTransferred += transferred;
        if(err == LIBUSB_ERROR_TIMEOUT && transferred > 0)//a timeout may indicate that some data was transferred, but not all
            err = 0;
    }while (totalTransferred < length*2 && err >= 0);
    
    //cout << "Transferred " << totalTransferred << " bytes\n";
    if (err < 0)
        throw libUSBError(err);
}

//Convenience wrapper around readScanData to get block of data into internal buffer.
void MCCDevice::getBlock()
{
    readScanData(mDataBuffer.data, mScanParams.samplesPerBlock*mScanParams.channelCount);
}

//scale and calibrate data
float MCCDevice::scaleAndCalibrateData(unsigned short data, int chanIdx){
    float calibratedData;
    float scaledAndCalibratedData;
    int fullScale = pMaxVoltage[chanIdx] - pMinVoltage[chanIdx];
    calibratedData = (float)data*pCalSlope[chanIdx] + pCalOffset[chanIdx];  // Gain and offset
    scaledAndCalibratedData = (calibratedData/(float)mMaxCounts)*fullScale + pMinVoltage[chanIdx];  // Scale by A2D range
    return scaledAndCalibratedData;
}

//Get the device input and output endpoints and the bulkPacketSize
void MCCDevice::getEndpoints()
{
    int descriptorType;
    int length;
    int index = 0;
    int numBytesTransferred;
    unsigned char epDescriptor[64]; //Is this preferred to above when size is constant?
    
    // Reset the member variables
    mEndpointIn = 0;
    mEndpointOut = 0;
    mBulkPacketSize = 0;
    
    //http://libusb.sourceforge.net/api-1.0/group__syncio.html
    numBytesTransferred = libusb_control_transfer(pDevHandle, STRINGMESSAGE, LIBUSB_REQUEST_GET_DESCRIPTOR,
                                                  (0x02 << 8) | 0, 0, epDescriptor, 64, 1000);
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
    
    while (true)
    {
        length = epDescriptor[index];
        descriptorType = epDescriptor[index + 1];
        if (length == 0)
            break;
        if (descriptorType == 0x05)
        {
            if ((epDescriptor[index + 2] & 0x80) != 0)
            {
                mEndpointIn = epDescriptor[index + 2];
                mBulkPacketSize = (unsigned short)(epDescriptor[index+5] << 8) | (unsigned short)(epDescriptor[index + 4]);
            }
            else
            {
                mEndpointOut = epDescriptor[index + 2];
            }
        }
        index += length;
        if (index >= numBytesTransferred)
            break;
    }
}

void MCCDevice::flushInputData()
{
    int bytesTransfered = 0;
    int status;
    do
    {
        unsigned char * buf = new unsigned char [mBulkPacketSize];
        status = libusb_bulk_transfer(pDevHandle, mEndpointIn, buf, mBulkPacketSize, &bytesTransfered, 200);
    } while (bytesTransfered > 0 && status == 0);
}
