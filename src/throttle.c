// throttling packets
#include "iup.h"
#include "common.h"
#define NAME "throttle"
#define CYCLE_MIN "0"
#define CYCLE_MAX "2000"
#define CYCLE_DEFAULT 100
#define ONTIME_MIN "0"
#define ONTIME_MAX "2000"
#define ONTIME_DEFAULT 20
#define PACKETRATE_MIN "5"
#define PACKETRATE_MAX "500"
#define PACKETRATE_DEFAULT 150
// threshold for how many packet to throttle at most
// #define KEEP_AT_MOST 1000
#define KEEP_AT_MOST 10000

static Ihandle *cycleInput, *onTimeInput, *packetRateInput, *dropThrottledCheckbox;

static volatile short throttleEnabled = 0,
    throttleInbound = 0, throttleOutbound = 1,
    chance = 10000, // [0-10000]
    // time frame in ms, when a throttle start the packets within the time 
    // will be queued and sent altogether when time is over
    cycleFrame = CYCLE_DEFAULT,
    onTime = ONTIME_DEFAULT,
    // Estimated number of packets per 1ms
    packetRate = PACKETRATE_DEFAULT,
    dropThrottled = 0; 

static PacketNode throttleHeadNode = {0}, throttleTailNode = {0};
static PacketNode *bufHead = &throttleHeadNode, *bufTail = &throttleTailNode;
static int bufSize = 0;
static DWORD throttleStartTick = 0;

static INLINE_FUNCTION short isBufEmpty() {
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle *throttleSetupUI() {
    Ihandle *throttleControlsBox = IupHbox(
        // dropThrottledCheckbox = IupToggle("Overflow flush", NULL),
        IupLabel("Cycle(ms):"),
        cycleInput = IupText(NULL),
        // IupLabel("On-time(ms):"),
        // onTimeInput = IupText(NULL),
        // IupLabel("# Packets/ms:"),
        // packetRateInput = IupText(NULL),
        // inboundCheckbox = IupToggle("Inbound", NULL),
        // outboundCheckbox = IupToggle("Outbound", NULL),
        // IupLabel("Chance(%):"),
        // chanceInput = IupText(NULL),
        NULL
        );

    // IupSetAttribute(chanceInput, "VISIBLECOLUMNS", "4");
    // IupSetAttribute(chanceInput, "VALUE", "100.0");
    // IupSetCallback(chanceInput, "VALUECHANGED_CB", uiSyncChance);
    // IupSetAttribute(chanceInput, SYNCED_VALUE, (char*)&chance);
    // IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    // IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&throttleInbound);
    // IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    // IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&throttleOutbound);
    // IupSetCallback(dropThrottledCheckbox, "ACTION", (Icallback)uiSyncToggle);
    // IupSetAttribute(dropThrottledCheckbox, SYNCED_VALUE, (char*)&dropThrottled);

    // sync cycle time
    IupSetAttribute(cycleInput, "VISIBLECOLUMNS", "3");
    IupSetAttribute(cycleInput, "VALUE", STR(CYCLE_DEFAULT));
    IupSetCallback(cycleInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    IupSetAttribute(cycleInput, SYNCED_VALUE, (char*)&cycleFrame);
    IupSetAttribute(cycleInput, INTEGER_MAX, CYCLE_MAX);
    IupSetAttribute(cycleInput, INTEGER_MIN, CYCLE_MIN);

    // sync on-time duration
    // IupSetAttribute(onTimeInput, "VISIBLECOLUMNS", "3");
    // IupSetAttribute(onTimeInput, "VALUE", STR(ONTIME_DEFAULT));
    // IupSetCallback(onTimeInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    // IupSetAttribute(onTimeInput, SYNCED_VALUE, (char*)&onTime);
    // IupSetAttribute(onTimeInput, INTEGER_MAX, ONTIME_MAX);
    // IupSetAttribute(onTimeInput, INTEGER_MIN, ONTIME_MIN);

    // sync packet rate
    // IupSetAttribute(packetRateInput, "VISIBLECOLUMNS", "3");
    // IupSetAttribute(packetRateInput, "VALUE", STR(PACKETRATE_DEFAULT));
    // IupSetCallback(packetRateInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    // IupSetAttribute(packetRateInput, SYNCED_VALUE, (char*)&packetRate);
    // IupSetAttribute(packetRateInput, INTEGER_MAX, PACKETRATE_MAX);
    // IupSetAttribute(packetRateInput, INTEGER_MIN, PACKETRATE_MIN);

    // enable by default to avoid confusing
    // IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    // IupSetAttribute(outboundCheckbox, "VALUE", "ON");
    // IupSetAttribute(dropThrottledCheckbox, "VALUE", "ON");

    if (parameterized) {
        // setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        // setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        // setFromParameter(chanceInput, "VALUE", NAME"-chance");
        setFromParameter(cycleInput, "VALUE", NAME"-cycle");
        // setFromParameter(onTimeInput, "VALUE", NAME"-ontime");
        // setFromParameter(packetRateInput, "VALUE", NAME"-rate");
    }

    return throttleControlsBox;
}

static void throttleStartUp() {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    } else {
        assert(isBufEmpty());
    }
    throttleStartTick = timeGetTime();
    startTimePeriod();
}

static void clearBufPackets(PacketNode *tail) {
    PacketNode *oldLast = tail->prev;
    throttleStartTick = timeGetTime();
    LOG("Throttled end, send all %d packets. Buffer at max: %s", bufSize, bufSize == KEEP_AT_MOST ? "YES" : "NO");
    while (!isBufEmpty()) {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
}

static void throttleCloseDown(PacketNode *head, PacketNode *tail) {
    UNREFERENCED_PARAMETER(tail);
    UNREFERENCED_PARAMETER(head);
    clearBufPackets(tail);
    endTimePeriod();
}

static short throttleProcess(PacketNode *head, PacketNode *tail) {
    PacketNode *pac = tail->prev;
    DWORD currentTick = timeGetTime();
    while (bufSize < KEEP_AT_MOST && pac != head) {
        if (checkDirection(pac->addr.Outbound, throttleInbound, throttleOutbound)) {
            insertAfter(popNode(pac), bufHead);
            ++bufSize;
            pac = tail->prev;
        } else {
            pac = pac->prev;
        }
    }

    // send all when throttled enough, including in current step
    if (bufSize >= KEEP_AT_MOST || (currentTick - throttleStartTick > (unsigned int)cycleFrame)) {
        clearBufPackets(tail);
    }

    return 1;
}

Module throttleModule = {
    "On/Off shaping",
    NAME,
    (short*)&throttleEnabled,
    throttleSetupUI,
    throttleStartUp,
    throttleCloseDown,
    throttleProcess,
    // runtime fields
    0, 0, NULL
};