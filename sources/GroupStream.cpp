#include "GroupStream.h"
#include "ParameterWriter.h"
#include "RTMFP.h"

using namespace std;
using namespace Mona;

GroupStream::GroupStream(UInt16 id) : FlashStream(id) {
	DEBUG("GroupStream ", id, " created")
}

GroupStream::~GroupStream() {
	disengage();
	DEBUG("GroupStream ",id," deleted")
}

bool GroupStream::process(PacketReader& packet,FlashWriter& writer, double lostRate) {

	UInt32 time(0);
	GroupStream::ContentType type = (GroupStream::ContentType)packet.read8();

	// if exception, it closes the connection, and print an ERROR message
	switch(type) {

		case GroupStream::GROUP_MEMBER: {
			string member, id;
			packet.read(PEER_ID_SIZE, member);
			INFO("NetGroup Peer ID added : ", Util::FormatHex(BIN member.data(), member.size(), id))
			OnNewPeer::raise(_groupId, id);
			break;
		}
		case GroupStream::GROUP_INIT: {
			if (packet.read16() != 0x4100) {
				ERROR("Unexpected format for NetGroup ID header")
				break;
			}
			string netGroupId, encryptKey, peerId;
			packet.read(0x40, netGroupId);
			if (packet.read16() != 0x2101) {
				ERROR("Unexpected format for NetGroup ID header")
				break;
			}
			packet.read(0x20, encryptKey);
			if (packet.read32() != 0x2303210F) {
				ERROR("Unexpected format for Peer ID header")
				break;
			}
			packet.read(PEER_ID_SIZE, peerId); 
			OnGroupHandshake::raise(netGroupId, encryptKey, peerId);
			break;
		}
		case GroupStream::GROUP_DATA: {
			string value;
			if (packet.available()) {
				UInt16 size = packet.read16();
				packet.read(size, value);
			}
			INFO("GroupStream ", id, " - NetGroup data message type : ", value)
			break;
		}
		case GroupStream::GROUP_NKNOWN2:
			INFO("GroupStream ", id, " - NetGroup 0E message type")
			OnGroupBegin::raise(_peerId, writer);
			break;
		case GroupStream::GROUP_REPORT: {
			INFO("GroupStream ", id, " - NetGroup Report (type 0A)")
			UInt8 size = packet.read8();
			while (size == 1) {
				packet.next();
				size = packet.read8();
			}
			if (size != 8) {
				ERROR("Unexpected 1st parameter size in group message 3")
				break;
			}
			OnGroupReport::raise(_peerId, packet, writer);
			break;
		}
		case GroupStream::GROUP_PLAY_PUSH:
			OnGroupPlayPush::raise(_peerId, packet, writer);
			break;
		case GroupStream::GROUP_PLAY_PULL:
			OnGroupPlayPull::raise(_peerId, packet, writer);
			break;
		case GroupStream::GROUP_INFOS: { // contain the stream name of an eventual publication
			string streamName, data;
			UInt8 sizeName = packet.read8();
			if (sizeName > 1) {
				packet.next(); // 00
				packet.read(sizeName-1, streamName);
				packet.read(packet.available(), data);
				OnGroupMedia::raise(_peerId, streamName, data, writer);
			}
			DEBUG("GroupStream ", id, " - Group Media Infos (type 21) : ", streamName)
			break;
		}
		case GroupStream::GROUP_FRAGMENTS_MAP:
			OnFragmentsMap::raise(_peerId, packet, writer);
			break;
		case GroupStream::GROUP_MEDIA_DATA: {

			UInt64 counter = packet.read7BitLongValue();
			DEBUG("GroupStream ", id, " - Group media message 20 : counter=", counter)
			if (*packet.current() == AMF::AUDIO || *packet.current() == AMF::VIDEO) {
				UInt8 mediaType = packet.read8();
				time = packet.read32();
				OnFragment::raise(_peerId, type, counter, 0, mediaType, time, packet, lostRate);
				break;
			}
			else
				return FlashStream::process(packet, writer, lostRate); // recursive call, can be invocation or other
		} case GroupStream::GROUP_MEDIA_START: { // Start a splitted media sequence

			UInt64 counter = packet.read7BitLongValue();
			UInt8 splitNumber = packet.read8(); // counter of the splitted sequence
			UInt8 mediaType = packet.read8();
			time = packet.read32();

			DEBUG("GroupStream ", id, " - Group ", (mediaType == AMF::AUDIO ? "Audio" : (mediaType == AMF::VIDEO ? "Video" : "Unknown")), " Start Splitted media : counter=", counter, ", time=", time, ", splitNumber=", splitNumber)
			if (mediaType == AMF::AUDIO || mediaType == AMF::VIDEO)
				OnFragment::raise(_peerId, type, counter, splitNumber, mediaType, time, packet, lostRate);
			else
				ERROR("Media type ", Format<UInt8>("%02X", mediaType), " not supported (or data decoding error)")
			break;
		}
		case GroupStream::GROUP_MEDIA_NEXT: { // Continue a splitted media sequence

			UInt64 counter = packet.read7BitLongValue();
			UInt8 splitNumber = packet.read8(); // counter of the splitted sequence
			DEBUG("GroupStream ", id, " - Group next Splitted media : counter=", counter, ", splitNumber=", splitNumber)
			OnFragment::raise(_peerId, type, counter, splitNumber, 0, 0, packet, lostRate);
			break;
		}
		case GroupStream::GROUP_MEDIA_END: { // End of a splitted media sequence

			UInt64 counter = packet.read7BitLongValue();
			DEBUG("GroupStream ", id, " - Group End splitted media : counter=", counter)
			OnFragment::raise(_peerId, type, counter, 1, 0, 0, packet, lostRate);
			break;
		}

		default:
			ERROR("GroupStream ", id, ", Unpacking type '",Format<UInt8>("%02X",(UInt8)type),"' unknown")
	}

	writer.setCallbackHandle(0);
	return writer.state()!=FlashWriter::CLOSED;
}


void GroupStream::messageHandler(const string& name, AMFReader& message, FlashWriter& writer) {
	/*** Player ***/
	if (name == "closeStream") {
		INFO("Stream ", id, " is closing...")
		// TODO: implement close method
		return;
	}
	
	FlashStream::messageHandler(name, message, writer);
}
