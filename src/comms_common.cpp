
#include "comms_common.h"
#include "string.h"




Crc32::Crc32() : 
    DefaultPolynomial(0x04C11DB7u),DefaultSeed(0xFFFFFFFFu),seed(0xFFFFFFFFu),hash(0xFFFFFFFFu){
    InitializeTable();
}


void Crc32::InitializeTable()
{
    uint32_t createTable[256];
    int32_t dividend;
    int32_t bit;

    for (dividend = 0; dividend < 256; dividend++)
    {
        uint32_t currByte = (uint32_t)((dividend << 24) & 0xFFFFFFFF);
        for (bit = 0; bit < 8; bit++)
        {
            if ((currByte & 0x80000000) != 0)
            {
                currByte <<= 1;
                currByte ^= this->DefaultPolynomial;
            }
            else
            {
                currByte <<= 1;
            }
        }

        createTable[dividend] = (currByte & 0xFFFFFFFF);
    }
    for (dividend = 0; dividend < 256; dividend++)
    {
        this->table[dividend] = createTable[dividend];
    }

}

uint32_t Crc32::CalculateHash(uint8_t buffer[], int start, int size)
{
    int32_t crc = this->seed;
    int32_t i;

    if (size % 4 != 0)
        return 0;

    for (i = start; i < start + size; i++){
        int32_t curByte = buffer[i] & 0xFF;
        crc = (uint32_t)((crc ^ (curByte << 24)) & 0xFFFFFFFF);
        int32_t pos = (crc >> 24) & 0xFF;
        crc = (crc << 8) & 0xFFFFFFFF;
        crc = (crc ^ this->table[pos]) & 0xFFFFFFFF;
    }

    return ((crc ^ 0x00000000) & 0xFFFFFFFF);
}

uint32_t Crc32::Compute(uint8_t buffer[], size_t size)
{
    return this->CalculateHash( buffer, 0, size);
}

uint32_t Crc32::Calculate(comms_payload_t* payload)
{
    if (!payload) return 0;
    size_t data = payload->data_size;
    if (data > BUFFER_DEFAULT_SIZE) return 0;

    size_t total = data + 4;
    size_t padded = (total + 3) & ~size_t(3);   // redondea a múltiplo de 4

    uint8_t buf[4 + BUFFER_DEFAULT_SIZE + 3] = {0}; // +3 por padding
    memcpy(buf + 0, &payload->msg_id, 2);
    memcpy(buf + 2, &payload->data_size, 2);
    memcpy(buf + 4, payload->data, data);
    // el resto queda a 0 por el inicializador

    for (size_t i = 0; i < padded; i += 4) {
        uint8_t t0 = buf[i + 0], t1 = buf[i + 1];
        buf[i + 0] = buf[i + 3];
        buf[i + 1] = buf[i + 2];
        buf[i + 2] = t1;
        buf[i + 3] = t0;
    }

    return this->Compute(buf, padded);
}




uint32_t Crc32::CalculateRaw(uint8_t* data, size_t size) {
    return this->Compute((uint8_t*)data, (size_t)(size));
}

bool Crc32::Check(comms_payload_t* payload) {
    if (!payload) return false;
    return payload->crc_32 == Calculate(payload);
}


uint8_t Crc32::CalculateCS(const uint8_t* data, uint8_t len) {
    if (!data || len == 0) return 0;
    uint32_t s = 0;
    for (uint8_t i = 0; i + 1 < len; ++i) s += data[i];
    return (uint8_t)(s & 0xFF);
}

#include <stddef.h>


#define FRAME_START_BYTE	0x7E
#define FRAME_STOP_BYTE 	0x7D
#define FRAME_SCAPE_BYTE 	0x7C
#define FRAME_STOP_COMB 	0x01
#define FRAME_START_COMB 	0x02
#define FRAME_SCAPE_COMB 	0x03

static uint8_t DecodeByte(uint8_t byte)
{
	if (byte == FRAME_STOP_COMB){
		return FRAME_STOP_BYTE;
	}else if (byte == FRAME_START_COMB){
		return FRAME_START_BYTE;
	}else if (byte == FRAME_SCAPE_COMB){
		return FRAME_SCAPE_BYTE;
	}else{
		return 0;
	}
}

static uint8_t EncodeByte(uint8_t byte)
{
	if (byte == FRAME_STOP_BYTE){
		return FRAME_STOP_COMB;
	}else if (byte == FRAME_START_BYTE){
		return FRAME_START_COMB;
	}else if (byte == FRAME_SCAPE_BYTE){
		return FRAME_SCAPE_COMB;
	}else{
		return 0;
	}
}


int DecodeFrame(uint8_t* frame, int frame_len, comms_payload_t* payload) {
    int dec_idx = 0;

    if (frame_len <= 0 || frame[0] != FRAME_START_BYTE) return -1;

    for (int i = 1; i < frame_len; i++) {
        uint8_t c = frame[i];

        if (c == FRAME_SCAPE_BYTE) {
            if (++i >= frame_len) return -1;
            uint8_t d = DecodeByte(frame[i]);
            if (d == 0) return -1;
            frame[dec_idx++] = d;
        }
        else if (c == FRAME_START_BYTE) {
            return -1;
        }
        else if (c == FRAME_STOP_BYTE) {
            if (!payload) return dec_idx;

            const size_t hdr_len = offsetof(comms_payload_t, data);

            if (dec_idx < (int)hdr_len) return -1;
            if (dec_idx > (int)(hdr_len + BUFFER_DEFAULT_SIZE)) return -1;

            memset(payload, 0, sizeof(*payload));
            memcpy(payload, frame, (size_t)dec_idx);

            if (payload->data_size > BUFFER_DEFAULT_SIZE) return -1;
            if (dec_idx != (int)(hdr_len + payload->data_size)) return -1;

            return dec_idx;
        }
        else {
            frame[dec_idx++] = c;
            // opcional: si dec_idx puede crecer demasiado, corta aquí también
            if (dec_idx > (int)(offsetof(comms_payload_t, data) + BUFFER_DEFAULT_SIZE)) return -1;
        }
    }
    return -1;
}


int EncodeFrame(comms_payload_t* payload, uint8_t* frame) {
    int fsize = 0;
    const uint8_t* Msg_Ptr = (const uint8_t*)payload;

    const size_t data_len = payload->data_size;
    if (data_len == 0 || data_len > BUFFER_DEFAULT_SIZE) return 0;

    const size_t hdr_len = offsetof(comms_payload_t, data);   // hasta data[]
    const size_t raw_len = hdr_len + data_len;                // header + data

    const uint8_t* msg_end_ptr = Msg_Ptr + raw_len;
    uint8_t* Frame_Ptr = frame;

    *Frame_Ptr++ = FRAME_START_BYTE;

    while (Msg_Ptr < msg_end_ptr) {
        uint8_t b = *Msg_Ptr++;
        if (b == FRAME_SCAPE_BYTE || b == FRAME_STOP_BYTE || b == FRAME_START_BYTE) {
            *Frame_Ptr++ = FRAME_SCAPE_BYTE;
            *Frame_Ptr++ = EncodeByte(b);
        }
        else {
            *Frame_Ptr++ = b;
        }
    }

    *Frame_Ptr++ = FRAME_STOP_BYTE;
    fsize = (int)(Frame_Ptr - frame);
    return fsize;
}