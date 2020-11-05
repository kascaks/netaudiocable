#ifndef _NAC_PCFILTERDESCRIPTOR_H_
#define _NAC_PCFILTERDESCRIPTOR_H_

#include <portcls.h>

static KSATTRIBUTE PinDataRangeSignalProcessingModeAttribute = {
	sizeof(KSATTRIBUTE),
	0,
	STATICGUIDOF(KSATTRIBUTEID_AUDIOSIGNALPROCESSING_MODE),
};

static PKSATTRIBUTE PinDataRangeAttributes[] = {
	&PinDataRangeSignalProcessingModeAttribute,
};

static KSATTRIBUTE_LIST PinDataRangeAttributeList = {
	ARRAYSIZE(PinDataRangeAttributes),
	PinDataRangeAttributes,
};

static KSDATARANGE_AUDIO SpeakerPinDataRangesStream = {
	{
		sizeof(KSDATARANGE_AUDIO),
		KSDATARANGE_ATTRIBUTES,         // An attributes list follows this data range
		0,
		0,
		STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
		STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
		STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
	},
	2,
	16,
	16,
	44100,
	44100
};

static
PKSDATARANGE SpeakerPinDataRangePointersStream[] = {
	PKSDATARANGE(&SpeakerPinDataRangesStream),
	PKSDATARANGE(&PinDataRangeAttributeList),
};

static KSDATARANGE SpeakerTopoPinDataRangesBridge[] = {
 {
   sizeof(KSDATARANGE),
   0,
   0,
   0,
   STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
   STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
   STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
 }
};

//=============================================================================
static PKSDATARANGE SpeakerTopoPinDataRangePointersBridge[] = {
  &SpeakerTopoPinDataRangesBridge[0]
};

static PCPIN_DESCRIPTOR SpeakerWaveMiniportPins[] = {
	// Wave Out Streaming Pin (Renderer) KSPIN_WAVE_RENDER_SINK_SYSTEM
	{
		1,
		1,
		0,
		NULL,        // AutomationTable
		{
			0,
			NULL,
			0,
			NULL,
			SIZEOF_ARRAY(SpeakerPinDataRangePointersStream),
			SpeakerPinDataRangePointersStream,
			KSPIN_DATAFLOW_IN,
			KSPIN_COMMUNICATION_SINK,
			&KSCATEGORY_AUDIO,
			NULL,
			0
		}
	},
	// Speaker Out Bridge Pin (Renderer) KSPIN_WAVE_RENDER_SOURCE
	{
		0,
		0,
		0,
		NULL,
		{
			0,
			NULL,
			0,
			NULL,
			SIZEOF_ARRAY(SpeakerTopoPinDataRangePointersBridge),
			SpeakerTopoPinDataRangePointersBridge,
			KSPIN_DATAFLOW_OUT,
			KSPIN_COMMUNICATION_NONE,
			&KSNODETYPE_SPEAKER,
			NULL,
			0
		}
	},
};

static PCCONNECTION_DESCRIPTOR SpeakerMiniportConnections[] = {
	//  FromNode,              FromPin,   ToNode,                      ToPin
	{   PCFILTER_NODE,         0,         PCFILTER_NODE,               1}
};

static PCAUTOMATION_TABLE AutomationSpeakerWaveFilter = {
	 sizeof(PCPROPERTY_ITEM), //                PropertyItemSize
  0,   //              PropertyCount
  NULL, //Properties;
   sizeof(PCMETHOD_ITEM), //                 MethodItemSize
  0, //                 MethodCount
  NULL, //   Methods;
  sizeof(PCEVENT_ITEM), //                 EventItemSize
  0, //                 EventCount;
  NULL, // Events
  0 //                 Reserved;
};

static PCFILTER_DESCRIPTOR SpeakerWaveMiniportFilterDescriptor = {
	0,                                              // Version
	&AutomationSpeakerWaveFilter,                   // AutomationTable
	sizeof(PCPIN_DESCRIPTOR),                       // PinSize
	SIZEOF_ARRAY(SpeakerWaveMiniportPins),          // PinCount
	SpeakerWaveMiniportPins,                        // Pins
	sizeof(PCNODE_DESCRIPTOR),                      // NodeSize
	0,                                              // NodeCount
	NULL,                                           // Nodes
	SIZEOF_ARRAY(SpeakerMiniportConnections),       // ConnectionCount
	SpeakerMiniportConnections,                     // Connections
	0,                                              // CategoryCount
	NULL                                            // Categories  - use defaults (audio, render, capture)
};

#endif