//
//  mccdevice.cpp
//
//  Created by Chadwick Boulay on 2014-03-12.
//
//

#include <iostream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <libusb.h>
#include "mccdevice.h"

using namespace std;

//Constructor finds the first available device where product ID == idProduct and optionally serial number == mfgSerialNumber
MCCDevice::MCCDevice(int idProduct)
{
    string mfgSerialNumber = "NULL";
    initDevice(idProduct, mfgSerialNumber);
}

MCCDevice::MCCDevice(int idProduct, string mfgSerialNumber)
{
    initDevice(idProduct, mfgSerialNumber);
}

//Destructor
MCCDevice::~MCCDevice () {
    //Free memory and devices
    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    libusb_free_device_list(list, true);
    libusb_exit(NULL);
    calSlope = nullptr;
    calOffset = nullptr;
    minVoltage = nullptr;
    maxVoltage = nullptr;
    delete [] mData;
    mData = nullptr;
}

//Find the device, opens it, and claims it. Called by constructors.
//Sets idProduct, maxCounts, list, dev_handle
void MCCDevice::initDevice(int idProduct, string mfgSerialNumber){
    int i;
    bool found = false;
    ssize_t sizeOfList;
    libusb_device_descriptor desc;
    libusb_device* device;
    string mfgsermsg = "?DEV:MFGSER";
    string retMessage;
    
    //Check if the product ID is a valid MCC product ID
    if(!isMCCProduct(idProduct))
    {
        throw MCC_ERR_INVALID_ID;
    }
    
    //Initialize USB libraries
    if(libusb_init(NULL) != 0)
    {
        throw MCC_ERR_USB_INIT;
    }
    
    //Get the list of USB devices connected to the PC
    sizeOfList= libusb_get_device_list(NULL, &list);
    
    //Traverse the list of USB devices to find the requested device
    for (i=0; (i<sizeOfList) && (!found); i++)
    {
        device = list[i];
        libusb_get_device_descriptor(device, &desc);
        if (desc.idVendor == MCC_VENDOR_ID && desc.idProduct == idProduct)
        {
            
            //Open the device
            //libusb_open(device, &dev_handle) returns -12 in Windows;
            if (!libusb_open(device, &dev_handle))
            {
                //Claim interface with the device
                if (!libusb_claim_interface(dev_handle, 0))
                {
                    //Get scan parameters
                    getScanParams(); //sets endpoint_in, endpoint_out, bulkPacketSize
                    
                    try
                    {
                        //get the device serial number
                        retMessage = sendMessage(mfgsermsg); //For 1608-FS-Plus, DEV:MFGSER=018FF921 in response to ?DEV:MFGSER
                    }
                    catch(mcc_err err)
                    {
                        throw err;
                    }
                    
                    //Erase message while keeping serial number
                    retMessage.erase(0, 11); //TODO: Use mfgsermsg.length()
                    //cout << "Found " << toNameString(idProduct) << " with Serial Number " << retMessage << "\n";
                    
                    //If the input serial number was not NULL and retMessage does not match (string.compare returns 0 if matched.
                    if (mfgSerialNumber.compare("NULL")!=0 && retMessage.compare(mfgSerialNumber)!=0)
                    {//serial numbers are not the same, release device and continue on
                        libusb_release_interface(dev_handle, 0);
                    }
                    else
                    { //serial numbers are the same, this is the correct device
                        found = true;
                    }
                }
            }
        }
    }
    
    if (!found)
    {
        throw MCC_ERR_NO_DEVICE;
    }
    else
    {
        this->idProduct = idProduct;
        maxCounts = 0xFFFF;//I deleted the firmware flash, so all devices initialize the same way.
                           //this->getLimits(); //For some reason, the messages do not get responses.
        
        //Always init the internal data buffer. It can be used with getBlock().
        //The data buffer can be ignored if using external data buffer and readScanData();
        mSamplesPerBlock = 1; //Change this value then reconfigure().
        mData = new unsigned short [mSamplesPerBlock * 1]; //This will get overwritten in reconfigure.
        this->reconfigure();
    }
}

//Get the device input and output endpoints
void MCCDevice::getScanParams()
{
    int numBytesTransferred;
    
    //unsigned char* epDescriptor = new unsigned char[64]; //endpoint descriptor
    unsigned char epDescriptor[64]; //Is this preferred to above when size is constant?
    
    numBytesTransferred = libusb_control_transfer(dev_handle, STRINGMESSAGE, LIBUSB_REQUEST_GET_DESCRIPTOR,
                                                  (0x02 << 8) | 0, 0, epDescriptor, 64, 1000);
    //http://libusb.sourceforge.net/api-1.0/group__syncio.html
    
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
    
    //Get pieces of information from epDescriptor (result of libusb_control_transfer).
    endpoint_in = getEndpointInAddress(epDescriptor, numBytesTransferred);
    endpoint_out = getEndpointOutAddress(epDescriptor, numBytesTransferred);
    bulkPacketSize = getBulkPacketSize(epDescriptor, numBytesTransferred);
    
    //delete[] epDescriptor;
}

//Find the input endpoint from an endpoint descriptor
unsigned char MCCDevice::getEndpointInAddress(unsigned char* data, int data_length)
{
    int descriptorType;
    int length;
    int index = 0;
    
    while (true)
    {
        length = data[index];
        descriptorType = data[index + 1];
        
        if (length == 0)
            break;
        
        if (descriptorType != 0x05)
        {
            index += length;
        }
        else
        {
            if ((data[index + 2] & 0x80) != 0)
                return data[index + 2];
            else
                index += length;
        }
        
        if (index >= data_length)
            break;
    }
    
    return 0;
}

//Find the output endpoint from an endpoint descriptor
unsigned char MCCDevice::getEndpointOutAddress(unsigned char* data, int data_length)
{
    int descriptorType;
    int length;
    int index = 0;
    
    while (true)
    {
        length = data[index];
        descriptorType = data[index + 1];
        
        if (length == 0)
            break;
        
        if (descriptorType != 0x05)
        {
            index += length;
        }
        else
        {
            if ((data[index + 2] & 0x80) == 0)
                return data[index + 2];
            else
                index += length;
        }
        
        if (index >= data_length)
            break;
    }
    
    return 0;
}

//Find the packet size from an endpoint descriptor.
unsigned short MCCDevice::getBulkPacketSize(unsigned char* data, int data_length){
    int descriptorType;
    int length;
    int index = 0;
    
    while(true){
        length = data[index];
        descriptorType = data[index+1];
        
        if(length == 0)
            break;
        
        if(descriptorType != 0x05){
            index += length;
        } else {
            if((data[index+2] & 0x80) != 0){
                //found the packet size
                return (unsigned short)(data[index+5] << 8) | (unsigned short)(data[index + 4]);
            } else {
                index += length;
            }
        }
    }
    
    return 0;
}

//Will return at most a 64 character array.
//Returns response if transfer successful, null if not
string MCCDevice::sendMessage(string message)
{
    try
    {
        sendControlTransfer(message);
        return getControlTransfer();
    }
    catch(mcc_err err)
    {
        throw err;
    }
}

//Send a message to the device
void MCCDevice::sendControlTransfer(string message)
{
    int numBytesTransferred;
    
    //StringUtil::toUpper(message);
    //TODO: Convert message toUpper
    
    //cout << "Sending: " << message << "\n";
    
    //Convert message to data, a char array exactly MAX_MESSAGE_LENGTH (64) long. Pad with zeros if necessary.
    uint16_t length = message.length();
    const char* msgData = message.data();
    unsigned char data[MAX_MESSAGE_LENGTH]; //64
    for (uint16_t i = 0; i < MAX_MESSAGE_LENGTH; i++) {
        data[i] = (i < length) ? msgData[i] : 0;
    }
    
    numBytesTransferred = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_ENDPOINT_OUT,
                                                  STRINGMESSAGE, 0, 0, data, MAX_MESSAGE_LENGTH, 1000);
    
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
}

//Receive a message from the device. This should follow a call to sendControlTransfer.
//It will return a pointer to at most a 64 character array.
string MCCDevice::getControlTransfer()
{
    int messageLength;
    unsigned char message[64];
    string strmessage;
    
    messageLength = libusb_control_transfer(dev_handle,  LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_ENDPOINT_IN,
                                            STRINGMESSAGE, 0, 0, message, 64, 1000);
    
    if(messageLength < 0)
        throw libUSBError(messageLength);
    
    strmessage = (char*)message;
    //cout << "Got: " << strmessage << "\n\n";
    return strmessage; //e.g. DEV:MFGSER=018FF921 in response to ?DEV:MFGSER
}


/*Reads analog in scan data.
 length is the length of the data array (max bytes to transfer)
 */
void MCCDevice::readScanData(unsigned short* data, int length)
{
    //A short is 16 bits. A char is 8 bits.
    int err = 0, totalTransferred = 0, transferred;
    unsigned char* dataAsByte = (unsigned char*)data; //Change the type of the pointer to data.
    unsigned int timeout = 2000000;///(bulkPacketSize*rate);
    
    do{
        //TODO: Convert to asynchronous I/O API
        err =  libusb_bulk_transfer(dev_handle, endpoint_in, &dataAsByte[totalTransferred], bulkPacketSize, &transferred, timeout);
        totalTransferred += transferred;
        //cout << "Transferred " << totalTransferred << "of " << length*2 << endl;
        /*if(err == LIBUSB_ERROR_TIMEOUT && transferred > 0)//a timeout may indicate that some data was transferred, but not all
         err = 0;*/
    }while (totalTransferred < length*2 && err >= 0); //TODO: Change 2 to bytes per sample.
    
    if (err < 0)
        throw libUSBError(err);
}

void MCCDevice::getBlock()
{
    readScanData(mData, mSamplesPerBlock*mChannelCount);
}

/*
 void MCCDevice::getLimits()
 {
 string respValidChans, respRate, respThru;
 vector<int> result;
 
 //Get the maximum scan rate (max per channel) and the maxthruput (total max).
 respRate = sendMessage("@AISCAN:MAXSCANRATE");//AISCAN:MAXSCANRATE=FIXED%100000
 myLimits.maxScanRate = fromString<int32_t>(respRate.erase(0, 25) );
 respThru = sendMessage("@AISCAN:MAXSCANTHRUPUT");//AISCAN:MAXSCANTHRUPUT=FIXED%400000
 myLimits.maxScanThruput = fromString<int32_t>( respThru.erase(0, 28) );
 cout << "maxScanRate=" << myLimits.maxScanRate << "; maxScanThruPut=" << myLimits.maxScanThruput << "\n\n";
 
 //Get the list of valid chans and pull out the first and last.
 respValidChans = sendMessage("?AI:VALIDCHANS");//AI:VALIDCHANS=0,1,2,3,4,5,6,7
 stringstream ss( respValidChans.erase(0, 14) );
 while( ss.good() )
 {
 string substr;
 getline( ss, substr, ',' );
 result.push_back( fromString<int>(substr) );
 }
 myLimits.lowChan = *min_element(result.begin(), result.end());
 myLimits.highChan = *max_element(result.begin(), result.end());
 cout << "Channels " << myLimits.lowChan << ":" << myLimits.highChan << "\n";
 }
 */

void MCCDevice::reconfigure()
{
    int lowChan, highChan;
    string respLow, respHigh, respOff, respSlope, respRange, respRate;
    stringstream strOff, strSlope, strRange;
    respLow = sendMessage("?AISCAN:LOWCHAN");
    lowChan = fromString<int>(respLow.erase(0, 15));
    respHigh = sendMessage("?AISCAN:HIGHCHAN");
    highChan = fromString<int>(respHigh.erase(0, 16));
    mChannelCount = highChan - lowChan + 1;
    respRate = sendMessage("?AISCAN:RATE");
    sampRate = fromString<float>(respRate.erase(0, 12));
    delete [] mData;
    mData = new unsigned short [mChannelCount * mSamplesPerBlock];
    
    //stringstream strBuff;
    //strBuff << "AISCAN:BUFSIZE=" << mChannelCount*mSamplesPerBlock*2;
    //sendMessage(strBuff.str());
    //sendMessage("AISCAN:BUFOVERWRITE=DISABLE");
    
    //Reset members that are per-channel arrays.
    calSlope = nullptr; calSlope = new float[mChannelCount];
    calOffset = nullptr; calOffset = new float[mChannelCount];
    minVoltage = nullptr; minVoltage = new int[mChannelCount];
    maxVoltage = nullptr; maxVoltage = new int[mChannelCount];
    
    for (int chanIdx = lowChan; chanIdx<=highChan; chanIdx++)
    {
        //Reset string streams.
        strOff.str("");
        strSlope.str("");
        strRange.str("");
        //strSlope = string("?AI{") + "}:SLOPE";
        strSlope << "?AI{" << chanIdx << "}:SLOPE";
        respSlope = sendMessage(strSlope.str());
        calSlope[chanIdx - lowChan] = fromString<float>(respSlope.erase(0,12));
        
        strOff << "?AI{" << chanIdx << "}:OFFSET";
        respOff = sendMessage(strOff.str());
        calOffset[chanIdx - lowChan] = fromString<float>(respOff.erase(0,13));
        //        calOffset[chanIdx-lowChan] = atof(respSlope.erase(0, 13).c_str());
        
        strRange << "?AI{" << chanIdx << "}:RANGE";
        respRange = sendMessage(strRange.str());
        respRange = respRange.erase(0, 12);
        if (respRange == "BIP10V"){
            minVoltage[chanIdx - lowChan] = -10;
            maxVoltage[chanIdx - lowChan] = 10;
        }else if (respRange == "BIP5V"){
            minVoltage[chanIdx - lowChan] = -5;
            maxVoltage[chanIdx - lowChan] = 5;
        }else if (respRange == "BIP2V"){
            minVoltage[chanIdx - lowChan] = -2;
            maxVoltage[chanIdx - lowChan] = 2;
        }else if (respRange == "BIP1V"){
            minVoltage[chanIdx - lowChan] = -1;
            maxVoltage[chanIdx - lowChan] = 1;
        }
        //cout << "Channel " << chanIdx << " Slope: " << calSlope[chanIdx-lowChan] << " Offset: " << calOffset[chanIdx-lowChan] << " in Range " << minVoltage[chanIdx-lowChan] << ":" << maxVoltage[chanIdx-lowChan] << "\n\n";
    }
}

//scale and calibrate data
float MCCDevice::scaleAndCalibrateData(unsigned short data, int chanIdx){
    float calibratedData;
    float scaledAndCalibratedData;
    int fullScale = maxVoltage[chanIdx] - minVoltage[chanIdx];
    
    //Calibrate the data
    calibratedData = (float)data*calSlope[chanIdx] + calOffset[chanIdx];
    
    //Scale the data
    scaledAndCalibratedData = (calibratedData/(float)maxCounts)*fullScale + minVoltage[chanIdx];
    
    return scaledAndCalibratedData;
}

void MCCDevice::flushInputData()
{
    int bytesTransfered = 0;
    int status;
    unsigned char * buf = new unsigned char [bulkPacketSize];
    do
    {
        status = libusb_bulk_transfer(dev_handle, endpoint_in, buf, bulkPacketSize, &bytesTransfered, 200);
    } while (bytesTransfered > 0 && status == 0);
    delete[] buf;
}
