#pragma once
#include "stdint.h"
#include "string.h"

#define BUFFER_DEFAULT_SIZE 8192

typedef struct comms_payload_t {
	uint16_t msg_id;
	uint32_t crc_32;
	uint16_t data_size;
	uint8_t data[BUFFER_DEFAULT_SIZE];
} comms_payload_t;

//////// CRC32 ///////
class Crc32 {

public:
	Crc32();
	uint32_t Calculate(comms_payload_t* payload);
	uint32_t CalculateRaw(uint8_t * data, size_t size);
	bool Check(comms_payload_t* payload);
	uint8_t CalculateCS(const uint8_t* data, uint8_t len);

private:
	uint32_t DefaultPolynomial;
	uint32_t DefaultSeed;
	uint32_t table[256];
	uint32_t seed;
	uint32_t hash;
	uint32_t CalculateHash(uint8_t buffer[], int start, int size);
	void InitializeTable();
	uint32_t Compute(uint8_t buffer[], size_t size);

};
//////////////////////

//////// Frame ///////
int DecodeFrame(uint8_t* frame, int frame_len, comms_payload_t* payload);
int EncodeFrame(comms_payload_t* payload, uint8_t* frame);
//////////////////////