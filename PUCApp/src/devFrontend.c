/* 
 * Support for CAEN A2620 Power Supplies
 * 
 * Author: W. Eric Norum
 * "2011/02/18 23:15:06 (UTC)"
 *
 * This code was inspired by the driver produced by Giulio Gaio
 * <giulio.gaio@elettra.trieste.it> but takes a considerably different
 * approach and requires neither the sequencer nor the IMCA library.
 *
 * Compile-time options:
 *   Uncomment the "#define ENABLE_TIMING_TESTS 1" line to enable code
 *   that measures the time taken to process a transaction.
 */

/************************************************************************\
* Copyright (c) 2011 Lawrence Berkeley National Laboratory, Accelerator
* Technology Group, Engineering Division
* This code is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <errlog.h>
#include <iocsh.h>
//#include "Command.h"
#include "asynDriver.h"
#include "asynOctetSyncIO.h"
#include "asynInt32.h"
#include "asynFloat64.h"
#include "drvAsynIPPort.h"
#include "devFrontend.h"
#include <epicsExport.h>

/*
 * Bits in configure 'flags' argument
 */
#define FLAG_SHOULD_INHIBIT_READBACK    0x1

/*
 * asynFloat64 subaddresses
 */
#define A_READ_SETPOINT_CURRENT          0
#define A_READ_READBACK_CURRENT          1
#define A_WRITE_SETPOINT_CURRENT         100

/*
 * asynInt32 subaddresses
 */
#define A_READ_POWER_ON             1
#define A_READ_GENERIC_FAULT        2
#define A_READ_DC_UNDER_V           3
#define A_READ_MOSFET_OVER_T        4
#define A_READ_SHUNT_OVER_T         5
#define A_READ_INTERLOCK            6
#define A_READ_INPUT_OVER_I         7
#define A_READ_CROWBAR              8
#define A_READ_SLEW_MODE            9
#define A_READ_FORCE_READBACK       99
#define A_WRITE_POWER_ON            100
#define A_WRITE_RESET               101
#define A_WRITE_SLEW_MODE           102

/*
 * Control byte bit assignmentss
 */
#define A26X_CONTROL_RESERVED      0x1    /* Must be set in all commands */
#define A26X_CONTROL_ENABLE_SLEW   0x10   /* Enable slew-rate limiting */
#define A26X_CONTROL_RESET_ERRORS  0x20   /* Reset latched errors */
#define A26X_CONTROL_POWER_ON      0x40   /* Turn supply on */
#define A26X_CONTROL_READBACK_ONLY 0x80   /* Perform readback only */

/*
 * Status byte bit assignmentss
 */
#define A26X_STATUS_POWER_ON      0x1    /* Supply is on */
#define A26X_STATUS_GENERIC_FAULT 0x2    /* Generic fault (hw error) */
#define A26X_STATUS_DC_UNDER_V    0x4    /* DC undervoltage */
#define A26X_STATUS_MOSFET_OVER_T 0x8    /* MOSFET overtemp */
#define A26X_STATUS_SHUNT_OVER_T  0x10   /* Shunt overtemp */
#define A26X_STATUS_INTERLOCK     0x20   /* Interlock chain broken */
#define A26X_STATUS_INPUT_OVER_I  0x40   /* Input overcurrent */
#define A26X_STATUS_CROWBAR       0x80   /* Crowbar (short circuit protect) */

/*
 * Supply slew rate (Amps/sec)
 */
#define A26X_SLEW_RATE  20

/*
 * Conditional-compile control
 */
/* #define ENABLE_TIMING_TESTS 1 */

/*
 * Readback values
 */
typedef struct a26xReadback {
    int    status;
    double setpointCurrent;
    double readbackCurrent;
} a26xReadback;

/*
 * Interposed layer private storage
 */
typedef struct FrontendPvt {
    asynUser      *pasynUser;      /* To perform lower-interface I/O */

    asynInterface  asynCommon;     /* Our interfaces */
    asynInterface  asynInt32;
    asynInterface  asynFloat64;

    a26xReadback   readback;       /* Most recent readback values */
    int            slewMode;       /* 0 or A26X_CONTROL_ENABLE_SLEW */

    unsigned long  commandCount;    /* Statistics */
    unsigned long  setpointUpdateCount;
    unsigned long  retryCount;
    unsigned long  noReplyCount;
    unsigned long  badReplyCount;

#ifdef ENABLE_TIMING_TESTS
    double         transMax;
    double         transAvg;
#endif

} FrontendPvt;

/*
 * Send command and get reply
 */
static asynStatus
sendCommand(asynUser *pasynUser, FrontendPvt *ppvt, int command, double setpoint)
{
    char sendBuf[80];
    char replyBuf[80];
    size_t nSend, nSent, nRecv;
    int eom;
    asynStatus status;
    int retry = 0;
#ifdef ENABLE_TIMING_TESTS
    epicsTimeStamp ts[2];
    double t;
#endif

    ppvt->commandCount++;
    nSend = sprintf(sendBuf, "FDB:%2.2X:%.4f\r",
                                    command | A26X_CONTROL_RESERVED, setpoint);
    for (;;) {
#       ifdef ENABLE_TIMING_TESTS
        epicsTimeGetCurrent(&ts[0]);
#       endif
        status = pasynOctetSyncIO->writeRead(ppvt->pasynUser, sendBuf, nSend,
                                             replyBuf, sizeof replyBuf - 1, 0.1,
                                             &nSent, &nRecv, &eom);
#       ifdef ENABLE_TIMING_TESTS
        epicsTimeGetCurrent(&ts[1]);
#       endif
        if (status == asynSuccess)
            break;
        if (++retry > 10) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                        "%s", ppvt->pasynUser->errorMessage);
            ppvt->noReplyCount++;
            return status;
        }
        ppvt->retryCount++;
    }
    replyBuf[nRecv] = '\0';
    if (sscanf(replyBuf, "#FDB:%X:%lf:%lf", &ppvt->readback.status,
                                            &ppvt->readback.setpointCurrent,
                                            &ppvt->readback.readbackCurrent) != 3) {
        char escBuf[80];
        epicsStrnEscapedFromRaw(escBuf, sizeof escBuf, replyBuf, nRecv);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                          "Bad reply string: \"%s\"", escBuf);
        ppvt->badReplyCount++;
        return asynError;
    }
#ifdef ENABLE_TIMING_TESTS
    t = epicsTimeDiffInSeconds(&ts[1], &ts[0]);
    if (t > ppvt->transMax) ppvt->transMax = t;
    ppvt->transAvg = ppvt->transAvg ? ppvt->transAvg * 0.998 + t * 0.002 : t;
#endif
    return asynSuccess;
}

/*
 * asynCommon methods
 */
static void
report(void *pvt, FILE *fp, int details)
{
    FrontendPvt *ppvt = (FrontendPvt *)pvt;

    if (details >= 1) {
#ifdef ENABLE_TIMING_TESTS
        fprintf(fp, "Transaction time avg:%.3g max:%.3g\n", ppvt->transAvg, ppvt->transMax);
        if (details >= 2) {
            ppvt->transMax = 0;
            ppvt->transAvg = 0;
        }
#endif
        fprintf(fp, "         Command count: %lu\n", ppvt->commandCount);
        fprintf(fp, " Setpoint update count: %lu\n", ppvt->setpointUpdateCount);
        fprintf(fp, "           Retry count: %lu\n", ppvt->retryCount);
        fprintf(fp, "        No reply count: %lu\n", ppvt->noReplyCount);
        fprintf(fp, "       Bad reply count: %lu\n", ppvt->badReplyCount);
    }
}

static asynStatus
connect(void *pvt, asynUser *pasynUser)
{
    return pasynManager->exceptionConnect(pasynUser);
}

static asynStatus
disconnect(void *pvt, asynUser *pasynUser)
{
    return pasynManager->exceptionDisconnect(pasynUser);
}
static asynCommon commonMethods = { report, connect, disconnect };

/*
 * asynInt32 methods
 */
static asynStatus
int32Write(void *pvt, asynUser *pasynUser, epicsInt32 value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;
	asynStatus status = asynError;
	
	//User can modify only the value
	size_t wrote;
	printf("Data writing Int32\n");	
		
	int bytesToWrite;
	
	//TODO:USE THE PROTOCOL!
	//TODO:USE nobts!
	char *result;
	result = (char *)malloc((3+1)*sizeof(char));
	result[0] = 0x20;//WRITE_VARIABLE;
	result[1] = 1+1;
	result[2] = pasynUser->reason;

	union {
		unsigned char c[4];
		epicsInt32 f;
	} u;
	u.f = value;
	printf("epicsInt32Value: %d\n",u.f);
	
	int i = 0;
	for(i = 0;i < 1; i++){
		result[3+i] = u.c[i];
	}
	
	for(i = 2;i < 7; i++)
		printf("message %d\n",result[i]);
	pasynOctetSyncIO->flush(ppvt->pasynUser);
	status = pasynOctetSyncIO->write(ppvt->pasynUser, result, (3+1)*sizeof(char), 5000, &wrote);
		
	if(status != asynSuccess) return status;
		
	//Read response from PUC		
	//char * bufferRead;
	//bufferRead = (char *) malloc(5*sizeof(char));
		
	//size_t bytesRead;
	//int eomReason;
		
	//status = pasynOctetSyncIO->read(user, bufferRead, 5, 5000, &bytesRead, &eomReason);
		
	//if(status != asynSuccess) return status;
		
	//printf("Read: %d\n", bufferRead[2]);			
	printf("Write: %d, Wrote: %li \n", bytesToWrite, wrote);	

	return status;
}

static asynStatus
int32Read(void *pvt, asynUser *pasynUser, epicsInt32 *value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;
	printf("Read Int32");
	asynStatus status = asynError;
	
	size_t wrote;
	
	printf("Sending request to read: %d\n",pasynUser->reason);
	
	int bytesToWrite;
	char *result;
	result = (char *) malloc(3*sizeof(char));
	bytesToWrite = 3*sizeof(char);
		
	result[0] = 0x10;//READ_VARIABLE
	result[1] = 1;
	result[2] = (pasynUser->reason) & 0xFF;
	fflush(stdout);
		
	status = pasynOctetSyncIO->write(ppvt->pasynUser, result, bytesToWrite, 5000, &wrote);
	
	if(status != asynSuccess) return status;
		
	//Read response from PUC
	//First, read the header, and after read the payload and checksum
		
	char * header;		
	char * payload;
	int size;
		
	size_t bytesRead;
	int eomReason;
		
	header = (char *) malloc(2*sizeof(char));
		
	printf("Reading\n");
	status = pasynOctetSyncIO->read(ppvt->pasynUser, header, 2, 5000, &bytesRead, &eomReason);		
	if(status != asynSuccess) return status;
				
	size = header[1];
	payload = (char *) malloc((size+1)*sizeof(char));
		
	status = pasynOctetSyncIO->read(ppvt->pasynUser, payload, size, 5000, &bytesRead, &eomReason);
	if(status != asynSuccess) return status;
	//TODO:NO THE BEST WAY TO DO THIS CONVERSION!!Use the protocol!	
	union{
		unsigned char c[4];
		epicsInt32 inp_i;
	}u;
	u.inp_i = 0;
	int i;
	for(i=0;i<size;i++){
		u.c[i] = payload[i];
	}
	*value = u.inp_i;
	//*value = com.readingVariable(header, payload,simple);
	
		
	return status;
}

static asynInt32 int32Methods = { int32Write, int32Read };

/*
 * asynFloat64 methods
 */
static asynStatus
float64Write(void *pvt, asynUser *pasynUser, epicsFloat64 value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;
	asynStatus status = asynError;
	
	//User can modify only the value
	size_t wrote;
	printf("Data writing\n");	
	printf("Value = %f\n", value);
	char *result;
	int bytesToWrite;
	int size=4;
	//char * write = com.writeVariable(0, sizeof(epicsFloat64), pasynUser->reason, (double) value, &bytesToWrite,simple);
	result = (char *) malloc ((3+size)*sizeof(char));
	bytesToWrite = (3+size)*sizeof(char);
	result[0] = 0x20;//WRITE_VARIABLE;
	result[1] = (size)+1;
	result[2] = (pasynUser->reason) & 0xFF;
	union {
               	unsigned char c[8];
               	double f;
        } u;
	int i;
	u.f = value;
	for(i = 0; i < 8; i++)
	{	
	result[3+i] = u.c[i];
	}
	printf("Send:  %d | %d | %d | | %u\n", result[0], result[1], result[2], result[size+2] & 0xFF);

	pasynOctetSyncIO->flush(ppvt->pasynUser);
	status = pasynOctetSyncIO->write(ppvt->pasynUser, result, bytesToWrite, 5000, &wrote);
		
	if(status != asynSuccess) return status;
		
	//Read response from PUC		
	//char * bufferRead;
	//bufferRead = (char *) malloc(5*sizeof(char));
		
	//size_t bytesRead;
	//int eomReason;
		
	//status = pasynOctetSyncIO->read(user, bufferRead, 5, 5000, &bytesRead, &eomReason);
		
	//if(status != asynSuccess) return status;
		
	//printf("Read: %d\n", bufferRead[2]);			
	printf("Write: %d, Wrote: %li \n", bytesToWrite, wrote);	

	return status;
}

static asynStatus
float64Read(void *pvt, asynUser *pasynUser, epicsFloat64 *value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;
	printf("Read float64\n");
	asynStatus status = asynError;
	
	size_t wrote;
	
	printf("Sending request to read\n");
	
	int bytesToWrite;
	char *result;
 	result = (char *) malloc(3*sizeof(char));
	bytesToWrite = 3*sizeof(char);
	result[0] = 0x10;//READ_VARIABLE;
	result[1] = 1;
	result[2] = (pasynUser->reason) & 0xFF;
	printf("Send: %d | %d | %d \n", result[0], result[1], result[2] & 0xFF);	
	
	status = pasynOctetSyncIO->write(ppvt->pasynUser, result, bytesToWrite, 5000, &wrote);
	
	if(status != asynSuccess) return status;
		
	//Read response from PUC
	//First, read the header, and after read the payload and checksum
		
	char * header;		
	char * payload;
	int size;
		
	size_t bytesRead;
	int eomReason;
		
	header = (char *) malloc(2*sizeof(char));
		
	printf("Reading\n");
	status = pasynOctetSyncIO->read(ppvt->pasynUser, header, 2, 5000, &bytesRead, &eomReason);		
	if(status != asynSuccess) return status;
				
	size = header[1];
	payload = (char *) malloc((size+1)*sizeof(char));
		
	status = pasynOctetSyncIO->read(ppvt->pasynUser, payload, size, 5000, &bytesRead, &eomReason);
	if(status != asynSuccess) return status;
	union {
               	unsigned char c[8];
               	double f;
       	} u;
	int i;
	for(i = 0; i < size; i++)
	{
		u.c[i] =  payload[i];
	}
	*value = u.f;
		
	return status;
}

static asynFloat64 float64Methods = { float64Write, float64Read };

epicsShareFunc int 
//devFrontendConfigure(const char *portName, const char *hostInfo, int flags, int priority)
devFrontendConfigure(const char *portName, const char *hostInfo, int priority)
{
    FrontendPvt *ppvt;
    char *lowerName, *host;
    asynStatus status;

    /*
     * Create our private data area
     */
    ppvt = callocMustSucceed(1, sizeof(FrontendPvt), "devFrontendConfigure");
    if (priority == 0) priority = epicsThreadPriorityMedium;

    /*
     * Create the port that we'll use for I/O.
     * Configure it with our priority, autoconnect, no process EOS.
     * We have to create this port since we are multi-address and the
     * IP port is single-address.
     */
    lowerName = callocMustSucceed(1, strlen(portName)+5, "devFrontendConfigure");
    sprintf(lowerName, "%s_TCP", portName);
    host = callocMustSucceed(1, strlen(hostInfo)+5, "devFrontendConfigure");
    sprintf(host, "%s TCP", hostInfo);
    drvAsynIPPortConfigure(lowerName, host, priority, 0, 1);
    status = pasynOctetSyncIO->connect(lowerName, -1, &ppvt->pasynUser, NULL);
    if (status != asynSuccess) {
        printf("Can't connect to \"%s\"\n", lowerName);
        return -1;
    }
    free(host);
    free(lowerName);

    /*
     * Create our port
     */
    status = pasynManager->registerPort(portName,
                                        ASYN_CANBLOCK,
                                        1,         /*  autoconnect */
                                        priority,  /* priority (for now) */
                                        0);        /* default stack size */
    if (status != asynSuccess) {
        printf("Can't register port %s", portName);
        return -1;
    }

    /*
     * Advertise our interfaces
     */
    ppvt->asynCommon.interfaceType = asynCommonType;
    ppvt->asynCommon.pinterface  = &commonMethods;
    ppvt->asynCommon.drvPvt = ppvt;
    status = pasynManager->registerInterface(portName, &ppvt->asynCommon);
    if (status != asynSuccess) {
        printf("Can't register asynCommon support.\n");
        return -1;
    }
    ppvt->asynInt32.interfaceType = asynInt32Type;
    ppvt->asynInt32.pinterface = &int32Methods;
    ppvt->asynInt32.drvPvt = ppvt;
    status = pasynManager->registerInterface(portName, &ppvt->asynInt32);
    if (status != asynSuccess) {
        printf("Can't register asynInt32 support.\n");
        return -1;
    }
    ppvt->asynFloat64.interfaceType = asynFloat64Type;
    ppvt->asynFloat64.pinterface = &float64Methods;
    ppvt->asynFloat64.drvPvt = ppvt;
    status = pasynManager->registerInterface(portName, &ppvt->asynFloat64);
    if (status != asynSuccess) {
        printf("Can't register asynFloat64 support.\n");
        return -1;
    }
    return 0;
}

/*
 * IOC shell command
 */
static const iocshArg devFrontendConfigureArg0 = { "port name",iocshArgString};
static const iocshArg devFrontendConfigureArg1 = { "host:port",iocshArgString};
static const iocshArg devFrontendConfigureArg2 = { "flags",iocshArgInt};
//static const iocshArg devFrontendConfigureArg3 = { "priority",iocshArgInt};
static const iocshArg *devFrontendConfigureArgs[] = {
                    &devFrontendConfigureArg0, &devFrontendConfigureArg1,
                    &devFrontendConfigureArg2/*, &devFrontendConfigureArg3*/ };
static const iocshFuncDef devFrontendConfigureFuncDef =
                      {"devFrontendConfigure",3,devFrontendConfigureArgs};
static void devFrontendConfigureCallFunc(const iocshArgBuf *args)
{
    devFrontendConfigure(args[0].sval, args[1].sval, args[2].ival);
}

static void
devFrontendConfigure_RegisterCommands(void)
{
    iocshRegister(&devFrontendConfigureFuncDef,devFrontendConfigureCallFunc);
}
epicsExportRegistrar(devFrontendConfigure_RegisterCommands);