/* Copyright (C) 2021 Kristian Sloth Lauszus and Dennis Frett. All rights reserved.

 This software may be distributed and modified under the terms of the GNU
 General Public License version 2 (GPL2) as published by the Free Software
 Foundation and appearing in the file GPL2.TXT included in the packaging of
 this file. Please note that GPL2 Section 2[b] requires that all works based
 on this software must also be made publicly available under the terms of
 the GPL2 ("Copyleft").

 Contact information
 -------------------

 Kristian Sloth Lauszus
 Web      :  https://lauszus.com
 e-mail   :  lauszus@gmail.com

 Dennis Frett
 GitHub   :  https://github.com/dennisfrett
 e-mail   :  dennis.frett@live.com

 Extensively reworked by 
 Alan Snyder
 GitHub : https://github.com/ajs123
 */

#include "MiniDSP.h"

// So far, this parser handles responses to 
//      the unary volume set (0x42), mute (0x17), and source (0x34) commmands
//      byte read (0x05) for certain known addresses
//      floating point read (0x14) for certain known addresses
//
// Known addresses for the 2xHD are
//      Byte values
//              FFD8            - Preset 0..3.  TBD: Verify that A8 is also the preset
//              FFD9 or FFA9    - Source 0..2 denoting Analog, TOSLINK, USB
//              FFDA            - Volume, in negative half-dB.  dB = -(value/2)
//              FFDB            - Mute 0, 1 where 1 = muted
//
//      Float values (4 bytes each)
//              0044            - Level input 1 in dB
//              0045            - Level input 2
//              0046 - 0049     - (output values not present in the 2x4HD)
//              004A            - Level output 1
//              004B            - Level output 2
//              004C            - Level output 3
//              004D            - Level output 4
//
// Byte read reports can be initiated in two ways that we know about:
//      1. In response to a request, such as 0x05 0xFF 0xDA 0x02 - read 2 bytes starting at FF DA (volume and mute)
//      2. Automatically, as an HID report, when changes are initiated with the remote, BUT only if the interface isn't busy with another request
// The automatic reports look like responses to a byte read request, so the same code handles either case.
//
// As far as we know, float read reports are only in response to a specific request.
//
// For the unary set commands, the minidsp responds with
// [0x01]       command response indicator
// [opcode]     the original command
// [data]       one data byte
//
// In response to the memory read commands and the equivalent HID reports, the minidsp provides
//
// [length]     length in bytes, including the length byte itself.
// [opcode]     the nature of the report. 
// [address_h]  see below
// [address_l]  see below
// [...]        data  ([length] - 4) bytes
// [check_byte]
//
// NOTE: All messages or 64 bytes long, so the len argument to ParseHIDData will always be 64.

void MiniDSP::parseDirectSetResponse(const uint8_t * buf) {

        bool presetChanged = false;
        bool sourceChanged = false;
        bool volumeChanged = false;
        bool mutedChanged = false;

        uint8_t data = buf[2];
        switch (buf[1])
        {
                case 0x42:
                        volumeChanged = static_cast<int>(data) != volume;
                        volume = static_cast<int>(data);
                        if ((callbackAlways || volumeChanged) && (pFuncOnVolumeChange != nullptr)) {
                                pFuncOnVolumeChange(volume);
                                }
                        break;
                case 0x17:
                        mutedChanged = data != muted;
                        muted = data;
                        if ((callbackAlways || mutedChanged) && (pFuncOnMutedChange != nullptr)) {
                                pFuncOnMutedChange(muted);
                                }
                        break;
                case 0x34:
                        sourceChanged = data != source;
                        source = data;
                        if ((callbackAlways || sourceChanged) && (pFuncOnSourceChange != nullptr)) {
                                pFuncOnSourceChange(source);
                                }
                        break;
        }
//        if (pFuncOnSourceChange != nullptr && sourceChanged) pFuncOnSourceChange(source);
//        if (pFuncOnVolumeChange != nullptr && volumeChanged)  pFuncOnVolumeChange(volume);
//        if (pFuncOnMutedChange != nullptr && mutedChanged) pFuncOnMutedChange(muted);
}

void MiniDSP::parseByteReadResponse(const uint8_t * buf) {

        bool presetChanged = false;
        bool sourceChanged = false;
        bool volumeChanged = false;
        bool mutedChanged = false;

        uint8_t dataLength = buf[0] - 4;
        uint8_t baseAddr = buf[3];
        for (uint8_t i = 0; i < dataLength; i++) // run through the address range
        {
                uint8_t addr = baseAddr + i;
                uint8_t data = buf[i + 4];
                switch (addr) {
                        case 0xD8:
                                presetChanged = data != preset;
                                preset = data;
                                break;
                                // if (pFuncOnPresetChange != nullptr && presetChanged) pFuncOnPresetChange(preset);
                        case 0xA9:
                        case 0xD9:
                                sourceChanged = data != source;
                                source = data;
                                if ((callbackAlways || sourceChanged) && (pFuncOnSourceChange != nullptr)) {
                                        pFuncOnSourceChange(source);
                                }
                                break;
                        case 0xDA:
                                volumeChanged = static_cast<int>(data) != volume;
                                volume = static_cast<int>(data);
                                if ((callbackAlways || volumeChanged) && (pFuncOnVolumeChange != nullptr)) {
                                        pFuncOnVolumeChange(volume);
                                }
                                break;
                        case 0xDB:
                                mutedChanged = data != muted;
                                muted = data;
                                if ((callbackAlways || mutedChanged) && (pFuncOnMutedChange != nullptr)) {
                                        pFuncOnMutedChange(muted);
                                }
                                break;
                }
        }
}

void MiniDSP::parseFloatReadResponse(const uint8_t * buf) {
        bool newOutputLevels = false; 
        bool newInputLevels = false;

        uint8_t dataLength = buf[0] - 4;        // bytes of data = message length - 4
        if ( (dataLength % 4) != 0 ) return;    // Ought to be a multiple of 4

        uint8_t nFloats = dataLength / 4;
        uint8_t baseAddr = buf[3];
        
        for (uint8_t i = 0; i < nFloats; i++)
        {
                uint8_t addr = baseAddr + i;                    // Low byte address of the floating point value
                float data = getFloatLE(buf + 4 + (i << 2));    // Step through the buffer in 4-byte (i << 2) steps
                switch (addr) {
                        case 0x44:              // 0x44 and 0x45 are the two inputs (at least for 2x4HD)
                                inputLevels[0] = data;
                                newInputLevels = true;
                                break;
                        case 0x45:
                                inputLevels[1] = data;
                                newInputLevels = true;
                                break;
                        case 0x4a:              // 0x4a - 0x4e are the four outputs
                                outputLevels[0] = data;
                                newOutputLevels = true;
                                break;
                        case 0x4b:
                                outputLevels[1] = data;
                                newOutputLevels = true;
                                break;
                        case 0x4c:
                                outputLevels[2] = data;
                                newOutputLevels = true;
                                break;
                        case 0x4d:
                                outputLevels[3] = data;
                                newOutputLevels = true;
                                break;
                }
        }
        if (pFuncOnNewOutputLevels != nullptr && newOutputLevels) pFuncOnNewOutputLevels(outputLevels);
        if (pFuncOnNewInputLevels != nullptr && newInputLevels) pFuncOnNewInputLevels(inputLevels);
}

void MiniDSP::ParseHIDData(USBHID *hid __attribute__ ((unused)), bool is_rpt_id __attribute__ ((unused)), uint8_t len, uint8_t *buf) {

        constexpr uint8_t readByteCommand = 0x05;       // Opcode and known high address for read bytes
        constexpr uint8_t readByteHighAddr = 0xFF;
        constexpr uint8_t readFloatCommand = 0x14;      // Opcode and known high address for read floats
        constexpr uint8_t readFloatHighAddr = 0x00;

        // Only care about valid data for the MiniDSP 2x4HD. 
        if (HIDUniversal::VID != MINIDSP_VID || HIDUniversal::PID != MINIDSP_PID || buf == nullptr) return;

        // For debugging
        if (pFuncOnParse != nullptr) pFuncOnParse(buf);

        // Check if this is a response to a direct set command
        // This is the only case in which buf[0] isn't the length of the whole message
        if (buf[0] == 0x01) parseDirectSetResponse(buf);
        
        // ... or a byte read.
        else if ((buf[1] == readByteCommand) && (buf[2] == readByteHighAddr)) parseByteReadResponse(buf);

        // ...or a floating point read
        else if ((buf[1] == readFloatCommand) && (buf[2] == readFloatHighAddr)) parseFloatReadResponse(buf);
}; 

float MiniDSP::getFloatLE(const uint8_t * buf) {
        float floater;
        memcpy(&floater, buf, 4);
        return floater;
}

uint8_t MiniDSP::OnInitSuccessful() {
        // Verify we're actually connected to the MiniDSP 2x4HD.
        if(HIDUniversal::VID != MINIDSP_VID || HIDUniversal::PID != MINIDSP_PID)
                return 0;

        // Request current status so we can initialize the values.
        //RequestStatus();

        if(pFuncOnInit != nullptr)
                pFuncOnInit();

        return 0;
};

uint8_t MiniDSP::Checksum(const uint8_t *data, uint8_t data_length) const {
        uint16_t sum = 0;
        for(uint8_t i = 0; i < data_length; i++)
                sum += data[i];

        return sum & 0xFF;
}

void MiniDSP::SendCommand(const uint8_t *command, uint8_t command_length) const {
        // Sanity check on command length.
        if(command_length > 63)
                return;

        // Message is padded to 64 bytes with 0xFF and is of format:
        // [ length (command + checksum byte) ] [ command ] [ checksum ] [ OxFF... ]

        // MiniDSP expects 64 byte messages.
        uint8_t buf[64];

        // Set length, including checksum byte.
        buf[0] = command_length + 1;

        // Copy actual command.
        memcpy(&buf[1], command, command_length);

        const auto checksumOffset = command_length + 1;

        // Set checksum byte.
        buf[checksumOffset] = Checksum(buf, command_length + 1);

        // Pad the rest.
        memset(&buf[checksumOffset + 1], 0xFF, sizeof (buf) - checksumOffset - 1);

        pUsb->outTransfer(bAddress, epInfo[epInterruptOutIndex].epAddr, sizeof (buf), buf);
}

void MiniDSP::RequestStatus() const {
        // Ask for volume, mute
        // uint8_t RequestStatusOutputCommand[] = {0x05, 0xFF, 0xDA, 0x02};

        // Ask for source, volume, mute
        constexpr uint8_t RequestStatusOutputCommand[] = {0x05, 0xFF, 0xD9, 0x03};

        // Ask for preset, source, volume, mute
        //constexpr uint8_t RequestStatusOutputCommand[] = {0x05, 0xFF, 0xD8, 0x04};

        SendCommand(RequestStatusOutputCommand, sizeof (RequestStatusOutputCommand));
}

void MiniDSP::requestSource() const {
        constexpr uint8_t requestSourceOutputCommand[] = {0x05, 0xFF, 0xD9, 0x01};
        SendCommand(requestSourceOutputCommand, sizeof(requestSourceOutputCommand));
}

void MiniDSP::requestVolume() const {
        constexpr uint8_t requestVolumeOuptutCommand[] = {0x05, 0xFF, 0xDA, 0x01};
        SendCommand(requestVolumeOuptutCommand, sizeof(requestVolumeOuptutCommand));
}

void MiniDSP::requestMute() const {
        constexpr uint8_t requestMuteOutputCommand [] = {0x05, 0xFF, 0xDB, 0x01};
        SendCommand(requestMuteOutputCommand, sizeof(requestMuteOutputCommand));
}

void MiniDSP::RequestOutputLevels() const {
        constexpr uint8_t RequestOutputLevelsCommand[] = {0x14, 0x00, 0x4a, 0x04};      // Four floats starting at 0x4A
        SendCommand(RequestOutputLevelsCommand, sizeof (RequestOutputLevelsCommand));
}

void MiniDSP::RequestInputLevels() const {
        constexpr uint8_t RequestInputLevelsCommand[] = {0x14, 0x00, 0x44, 0x02};       // Two floats starting at 0x44
        SendCommand(RequestInputLevelsCommand, sizeof (RequestInputLevelsCommand));
}

void MiniDSP::RequestLevels() const {
        constexpr uint8_t RequestAllLevelsCommand[] = {0x14, 0x00, 0x44, 0x0A};         // Ten floats starting at 0x44. Four (0x46 - 0x49) are unused.
        SendCommand(RequestAllLevelsCommand, sizeof(RequestAllLevelsCommand));
}

void MiniDSP::setVolume(float volume)
{
        uint8_t intVol = max(-127, min(0, volume)) * 2; 
        setVolume(intVol);
}

void MiniDSP::setVolume(uint8_t volume)
{
        uint8_t buf[2];
        buf[0] = 0x42;
        buf[1] = volume;
        SendCommand(buf, 2);
}

void MiniDSP::setMute(bool muteOn)
{
        uint8_t buf[2];
        buf[0] = 0x17;
        buf[1] = muteOn ? 0x01 : 0x00;
        SendCommand(buf, 2);
}

void MiniDSP::setSource(uint8_t source)
{
        if (source > 1) return;
        uint8_t buf[2];
        buf[0] = 0x34;
        buf[1] = source;
        SendCommand(buf, 2);
}