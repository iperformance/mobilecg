#include "ECGSender.h"
#include "ADS1298.h"
#include "Bluetooth.h"

const float ECGSender::ECG_LSB_IN_MV = 0.0001430511475f;

ECGSender::ECGSender(Packetizer &iPacketizer):
	compressFifo((char*)compressBuffer, ECG_COMPRESS_OUTPUT_BUFFER_SIZE),
	compressor(compressFifo, ecgPredictor),
	testGenerator(15000,500)
{
	packetizer = &iPacketizer;
	testSignal = false;

	currLsbInMv = ECG_LSB_IN_MV/6.0f;
	currFrequency = 488.28125f;
}

ECGSender::~ECGSender() {
}

static volatile int dummyCnt = 0;

void ECGSender::send(){
	//Create header and calculate data size
	uint8_t header[Packetizer::HEADER_SIZE + sizeof(ECGHeader)];
	ECGHeader *ecgHeader = (ECGHeader *)(header + Packetizer::HEADER_SIZE);

	ecgHeader->lsbInMv = currLsbInMv;
	ecgHeader->samplingFrequency = currFrequency;
	ecgHeader->channelCount = ADS1298::instance().getActiveChannels();

	ADS1298::EcgBuffer& buffer = ADS1298::instance().getBuffer();
	uint32_t size = buffer.used();
	uint32_t blockSize = (ecgHeader->channelCount+1)*3;

	if (size>ECG_MAX_SEND_SIZE)
		size=ECG_MAX_SEND_SIZE;

	size -= size % blockSize;
	ecgHeader->sampleCount = size / blockSize;

	if (size==0){
		return;
	}

	//Initialize the compressor
	compressFifo.reset();
	ecgPredictor.reset();
	ecgPredictor.setNumChannels(ecgHeader->channelCount);
	compressor.setNumChannels(ecgHeader->channelCount);

	//Compress
	for (unsigned pos=0; pos<size; pos+=blockSize){
		buffer.get((uint8_t*)&tempBlock, blockSize);
		ADS1298::fixByteOrder(&tempBlock);
		/*int test = testGenerator.getSample();
		testGenerator.next();
		tempBlock.channel1 = test;
		tempBlock.channel2 = test;
		tempBlock.channel3 = test;
		tempBlock.channel4 = test;
		tempBlock.channel5 = test;
		tempBlock.channel6 = test;
		tempBlock.channel7 = test;
		tempBlock.channel8 = test; // uoeuao */

		if (testSignal){
			for (int a=0; a<ecgHeader->channelCount; a++){
				sampleOfChannels[a]=testGenerator.getSample(testGenerator.getPeriod()*a/ecgHeader->channelCount);
				testGenerator.next();
			}
		} else {
			//Convert the 24 bit output format of the ECG to something usable
			sampleOfChannels[0]=tempBlock.channel1;
			sampleOfChannels[1]=tempBlock.channel2;
			sampleOfChannels[2]=tempBlock.channel3;
			sampleOfChannels[3]=tempBlock.channel4;
			sampleOfChannels[4]=tempBlock.channel5;
			sampleOfChannels[5]=tempBlock.channel6;
			sampleOfChannels[6]=tempBlock.channel7;
			sampleOfChannels[7]=tempBlock.channel8;
		}

		//uint8_t* dummy;
		//int dummySize = buffer.getContinuousWriteBuffer(dummy);
		//sampleOfChannels[1] = dummySize;

		//Compress
		compressor.putSample(sampleOfChannels);
	}

	ecgHeader->numBits = compressFifo.getAvailableBits();
	size = (ecgHeader->numBits+7) / 8;


	//Send header
	packetizer->startPacket(header, Packetizer::ECG, (uint16_t)(size+sizeof(ECGHeader)));
	packetizer->checksumBlock((uint8_t*)ecgHeader, sizeof(ECGHeader));
	if (Bluetooth::instance().send((char*)header, Packetizer::HEADER_SIZE + sizeof(ECGHeader), TIME_INF, false)<=0){
		return;
	}

	packetizer->checksumBlock(compressBuffer, size);
	if (Bluetooth::instance().send((char*)compressBuffer, size, TIME_INF, false)<=0){
		return;
	}

	//Send checksum
	Packetizer::Checksum chksum = packetizer->getChecksum();
	if (Bluetooth::instance().send((char*)&chksum, sizeof(chksum), TIME_INF, false)<=0){
		return;
	}

	//Start transmit if not running yet
	Bluetooth::instance().send(NULL,0);
}
