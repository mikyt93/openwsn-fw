#include "opendefs.h"
#include "schedule.h"
#include "openserial.h"
#include "opentimers.h"
#include "openrandom.h"
#include "sixtop.h"
#include "IEEE802154.h"
#include "myschedule.h"

//Global Variable
myschedule_vars_t myschedule_vars;
//Private function prototypes
void mySchedule_scheduleNewNeighbor(OpenQueueEntry_t* msg);
void timers_mySchedule_fired(void);
void mySchedule_init(void){
	memset(&myschedule_vars,0,sizeof(myschedule_vars_t));
		myschedule_vars.state	=MYSCHEDULE_REQUEST;
		myschedule_vars.busySending	=FALSE;
		myschedule_vars.timerld	=opentimers_start(MYSCHEDULETIMERPERIOD,TIMER_PERIODIC,TIME_MS,timers_mySchedule_fired);
}

void timers_mySchedule_fired(void){
	switch (myschedule_vars.state){
	case MYSCHEDULE_REQUEST:
		mySchedule_sendReq();
		break;
	case MYSCHEDULE_RESPONSE:
		mySchedule_sendRes();
		break;
	case MYSCHEDULE_STABLE:
		break;
	default:
		break;
	}
}

/* questa funzione serve per gestire i pacchetti di commando ricevuti da un nodo
 in base al tipo di pacchetto ricevuto si chiama la funzione opportuna */
void mySchedule_receive(OpenQueueEntry_t* msg){
	uint8_t command;
	command=msg->payload[0];
	switch (command){
	case IEEE154_CMD_REQ:
		mySchedule_processReq(msg);
		break;
	case IEEE154_CMD_RES:
		mySchedule_processRes(msg);
		break;
	default:
		break;
	}
	openqueue_freePacketBuffer(msg);
}

port_INLINE void mySchedule_sendReq(void){
	/* OpenQueueEntry_t è una struttura dati definita in opendefs.h che contiene le informazioni relative ad un pacchetto */
	OpenQueueEntry_t* pkt;
	/* open_addr_t è una struttura dati definita in opendefs.h per memorizzare gli indirizzi IP */
	open_addr_t parent_Addr;
	/* prima di creare un pacchetto ed inviarlo bisogna fare alcuni controlli */

	/* --1-- Verificare che il nodo sia sincronizzato */
	if (ieee154e_isSynch()==FALSE){
		//Non è sincronizzato, cancello i pacchetti generati da questo modulo
		openqueue_removeAllCreatedBy(COMPONENT_MYSCHEDULE);
		//Ora è occupato per inviare un commando
		myschedule_vars.busySending=FALSE;
		//stop
		return;
	}

	/* --2-- Verificare che non sta già trasmettendo la stessa informazione */
	if (neighbors_getMyDAGrank()==0){
		//non proseguire
		return;
	}

	/* --3-- Verificare che esiste un nodo parent a cui inviare la richiesta */
	if (neighbors_getPrefferedParentEui64(&parentAddr)==FALSE){
		//non proseguire
		return;
	}

	//if i get here, i will send a request command

	//get a free packet buffer
	pkt=openqueue_getFreePacketBuffer(COMPONENT_MYSCHEDULE);

	/* --4-- Verificare che ci sia spazio nel buffer */
	if (pkt==NULL){
		openserial_printError(COMPONENT_MYSCHEDULE,ERR_NO_FREE_PACKET_BUFFER,
				(errorparameter_t)1,
				(errorparameter_t)0);
		return;
	}
	/* tutte le verifiche sono superate e si procede con la preparazione del pacchetto */

	//declarare ownership over that packet
	pkt->creator=COMPONENT_MYSCHEDULE;
	pkt->owner=COMPONENT_MYSCHEDULE;
	//some L2 information for this packet
	pkt->l2_frameType=IEEE154_TYPE_CMD;
	neighbors_getPreferredParentEui64(&(pkt->l2_nextORpreviousHop));
	//Insert the command type and payload here
	packetfunction_reserveHeaderSize(pkt,sizeof(IEEE154_CMD_REQ));
	pkt->payload[0]=IEEE154_CMD_REQ;
	//put in queue for MAC to handle
	sixtop_send_internal(pkt,IEEE154_IELIST_NO,IEEE154_FRAMEVERSION_2006);
	//I'm now busy sending
	myschedule_vars.busySending=TRUE;
}

void mySchedule_processReq(OpenQueueEntry_t* msg){
	uint8_t i;
	open_addr_t* prefix;
	open_addr_t* address64bit;
	for(i=0;i<MAXNEIGHBORS;i++){
		if(packetfunctions_sameAddress(&msg->l2_nextORpreviousHop,&myschedule_vars.scheduleBuf[i].address)&&myschedule_vars.scheduleBuf[i].isUsed==TRUE){
			//Neighbor already scheduled, just send a response
			myschedule_vars.relativePosition=i;
			myschedule_vars.state=MYSCHEDULE_RESPONSE;
			return;
		}
	}
	//if it is here a new entry in the neighbors table and schedule to be done
	//adds new scheduled slots and updates the related variables
	mySchedule_scheduleNewNeighbor(msg);
	//change the state machine in RESPONSE MODE
	myschedule_vars.state=MYSCHEDULE_RESPONSE;
	mySchedule_sendRes();
}

port_INLINE void mySchedule_sendRes(void){
	//the payload should contain the cannel offset and the slotofset
	OpenQueueEntry_t* pkt;
	open_addr_t* destinationAddr;
	if(ieee154e_isSynch()==FALSE){
		//I'm not syncked
		//delete packets generated by this module
		openqueue_removeAllCreatedBy(COMPONENT_MYSCHEDULE);
		//I'm now busy sending a command
		myschedule_vars.busySending=FALSE;
		//stop here
		return;
	}
	if(myschedule_vars.busySending==TRUE){
		//don't proceed if I'm still sending a command
		return;
	}
	//if I get here, I will send a response command
	//get a free packet buffer
	pkt=openqueue_getFreePacketBuffer(COMPONENT_MYSCHEDULE);
	if(pkt==NULL){
		openserial_printError(COMPONENT_MYSCHEDULE,ERR_NO_FREE_PACKET_BUFFER,
				(errorparameter_t)1,
				(errorparameter_t)0);
		return;
	}
	//declare ownership over that packet
	pkt->creator=COMPONENT_MYSCHEDULE;
	pkt->owner=COMPONENT_MYSCHEDULE;
	//some L2 information about this packet
	pkt->l2_frameType=IEEE154_TYPE_CMD;
	destionAddr=&myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].address;
	memcpy(&(pkt->l2_nextORpreviousHop),destionationAddr,sizeof(open_addr_t));

	//Insert the command type and payload here
	packetfunctions_reserveHeaderSize(pkt,sizeof(IEEE154_CMD_REQ)+2);
	pkt->payload[0]=IEEE154_CMD_RES;
	pkt->payload[1]=myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].relative_slot;
	pkt->payload[2]=myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].relative_channel;
	//put in queue for MAC to handle
	sixtop_send_internal(pkt,IEEE154_IELIST_NO,IEEE154_FRAMEVERSIONE_2006);
	//I'm now busy sending
	myschedule_vars.busySending=TRUE;
}

void mySchedule_processRes(OpenQueueEntry_t* msg){
	uint8_t tx_slotOffset,rx_slotOffset;
	uint8_t tx_channelOffset,rx_channelOffset;
	open_addr_t temp_neighbor;
	rx_slotOffset=STARTOFSCHEDULE+myschedule_vars.relativePosition;
	tx_slotOffset=STARTOFSCHEDULE+myschedule_vars.relativePosition+MAXNEIGHBORS;

	rx_channelOffset=msg->payload[2];
	tx_channelOffset=msg->payload[2];

	memcpy(&myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].address,&(msg->l2_nextORpreviousHop),sizeof(open_addr_t));
	myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].relative_slot=myschedule_vars.relativePosition;
	myschedule_vars.scheduleBuf[myschedule_vars.relativePosition].relative_channel=tx_channelOffset;

	//add the new active slot
	schedule_addActiveSlot(
			rx_slotOffset,	//slot offset
			CELLTYPE_RX,	//type of slot
			FALSE,			//shared?
			rx_channelOffeset,	//channel offset
			&msg->l2_nextORpreviousHop	//neighbor
			);

	schedule_addActiveSlot(
			tx_slotOffset,	//slot offset
			CELLTYPE_TX,	//type of slot
			FALSE,			//shared?
			tx_channelOffset,	//channel offset
			&msg->l2_nextORpreviousHop	//neighbor
			);
	myschedule_vars.state=MYSCHEDULE_STABLE;
}

void mySchedule_notifySendDone(OpenQueueEntry_t* msg){
	myschedule_vars.busySending=FALSE;
	if(myschedule_vars.state==MYSCHEDULE_RESPONSE){
		myschedule_vars.state=MYSCHEDULE_STABLE;
	}
	openqueue_freePacketBuffer(msg);
}

//Private Functions
void mySchedule_scheduleNewNeighbor(OpenQueueEntry_t* msg){
	uint16_t tx_slotOffset,rx_slotOffset;
	uint16_t tx_channelOffset,rx_channelOffset;
	open_addr_t temp_neighbor;
	uint8_t i;
	slotOffset_t running_slotOffset;

	//reset local variables
	schedule_reset();
	//set frame length
	schedule_setFrameLength(SUPERFRAME_LENGTH);
	//start at slot 0
	running_slotOffset=0;
	//advertisement slots
	memset(&temo_neighbor,0,sizeof(temp_neighbor));
	for(i=0;i<NUMADVSLOTS;i++){
		schedule_addActiveSlot(
				running_slotOffset,	//slot offset
				CELLTYPE_ADV,	//type of slot
				FALSE,			//shared??
				0,				//channel offset
				&temp_neighbor	//neighbor
				);
		running_slotOffset++;
	}

	//shared TXRX anycast slots
	memset(&temp_neighbor,0,sizeof(temp_neighbor));
	temp_neighbor.type=ADDR_ANYCAST;
	for(i=0;i<NUMSHAREDTXRX;i++){
		schedule_addActiveSlot(
				running_slotOffset, //slot offset
				CELLTYPE_TXRX,		//type of slot
				TRUE,				//shared??
				0,					//channel offset
				&temp_neighbor		//neighbor
				);
		running_slotOffset++;
	}
	memcpy(&temp_neighbor,&(msg->l2_nextORpreviousHop),sizeof(open_addr_t));
	for(i=0;i<MAXNEIGHBOR;i++){
		if(myschedule_vars.scheduleBuf[i].isUsed==FALSE){
			myschedule_vars.relativePosition=i;
			tx_slotOffset=STARTOFSCHEDULE+i;
			rx_slotOffset=STARTOFSCHEDULE+i+MAXNEIGHBORS;
			myschedule_vars.scheduleBuf[i].relative_slot=i;
			tx_channelOffset=openrandom_get16b()%16;
			rx_channelOffset=tx_channelOffset;
			myschedule_vars.scheduleBuf[i].relative_channel=tx_channelOffset;
			memcpy(&myschedule_vars.scheduleBuf[i].address,&(msg->l2_nextORpreviousHop),sizeof(open_addr_t));

			//add the new active slot
			schedule_addActiveSlot(
					tx_slotOffset,	//slot offset
					CELLTYPE_TX,	//type of slot
					FALSE,			//shared?
					tx_channelOffset,	//channel offset
					&temp_neighbor	//neighbor
					);
			schedule_addActiveSlot(
					rx_slotOffset,	//slot offset
					CELLTYPE_RX,	//type of slot
					FALSE,			//shared?
					rx_channelOffset,	//channel offset
					&temp_neighbor	//neighbor
					);
			break;
		}
	}
	running_slotOffset=90;

	//serial RX slot
	memset(&temp_neighbor,0,sizeof(temp_neighbor));
	schedule_addActiveSlot(
			running_slotOffset,		//slot offset
			CELLTYPE_SERIALRX,		//type of slot
			FALSE,					//shared??
			0,						//channel offset
			&temp_neighbor			//neighbor
			);
	running_slotOffset++;
}
