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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

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
#include "asynCommonSyncIO.h"
#include "asynStandardInterfaces.h"
#include "drvAsynIPPort.h"
#include "devFrontend.h"
#include <epicsExport.h>
#include "unionConversion.h"
#include "sendrecvlib.h"
#include "frontendRecordParams.h"

/*
 * Interposed layer private storage
 */
typedef struct FrontendPvt {
    asynUser      *pasynUser;      /* To perform lower-interface I/O */

    asynInterface  asynCommon;     /* Our interfaces */
    asynInterface  asynInt32;
    asynInterface  asynFloat64;
    asynInterface  asynDrvUser;

    unsigned long commandCount;
    unsigned long setpointUpdateCount;
    unsigned long retryCount;
    unsigned long noReplyCount;
    unsigned long badReplyCount;

    char *serverAddress;

    sllp_client_t *sllp;
    struct sllp_vars_list *vars;

} FrontendPvt;

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
drvUserCreate(void *drvPvt, asynUser *pasynUser,const char *drvInfo, const char **pptypeName, size_t *psize)
{
	//int offset;
	char *pstring = NULL;
	/* We are passed a string that identifies this command.
	* Set dataType and/or pasynUser->reason based on this string */

	/*for (i=0; i<FRONTEND_N_PARAMS; i++) {
		if (epicsStrCaseCmp(drvInfo, pstring) == 0) {
			pasynManager->getAddr(pasynUser, &offset);
			if ((offset < 0) ) {
				asynPrint(pPlc->pasynUserTrace, ASYN_TRACE_ERROR,"%s::drvUserCreate port %s invalid memory request %d, max=%d\n",driver, pPlc->portName, offset, pPlc->modbusLength);
                		return asynError;
            		}
		pasynUser->reason = modbusDataCommand;
		if (pptypeName) *pptypeName = epicsStrDup(MODBUS_DATA_STRING);
		if (psize) *psize = sizeof(MODBUS_DATA_STRING);
		asynPrint(pasynUser, ASYN_TRACE_FLOW,"%s::drvUserCreate, port %s data type=%s\n", driver, pPlc->portName, pstring);
            return asynSuccess;
		}
	}*/

	return frontendparamProcess(pasynUser,pstring,drvInfo,pptypeName,psize);
}
static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser, const char **pptypeName, size_t *psize)
{
	int command = pasynUser->reason;
	if (pptypeName)
		*pptypeName = epicsStrDup(FrontendParam[command].paramString);
	if(psize) *psize = sizeof(command);
	return asynSuccess;
}
static asynStatus drvUserDestroy(void *drvPvt, asynUser *pasynUser){
	return asynSuccess;
}
static asynDrvUser drvUser = { drvUserCreate,drvUserGetType, drvUserDestroy };


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
	unsigned_int_32_value ui32v;
	ui32v.ui32value = (uint32_t) value;
	struct sllp_var_info * var = &ppvt->vars->list[pasynUser->reason];

	if(sllp_write_var(ppvt->sllp, var, ui32v.vvalue)!=SLLP_SUCCESS)
	{
    		if( pasynOctetSyncIO->connect(ppvt->serverAddress, -1, &ppvt->pasynUser, NULL) != asynSuccess)
			printf("SERVER DISCONNECTED\n");
		return asynError;
	}
	return asynSuccess;
}

static asynStatus
int32Read(void *pvt, asynUser *pasynUser, epicsInt32 *value)
{

	FrontendPvt *ppvt = (FrontendPvt *)pvt;

	uint8_t *val;
	val = (uint8_t*) malloc(4*sizeof(char));
	
	struct sllp_var_info * var = &ppvt->vars->list[pasynUser->reason];

	if(sllp_read_var(ppvt->sllp, var, val)!=SLLP_SUCCESS)
	{
    		if( pasynOctetSyncIO->connect(ppvt->serverAddress, -1, &ppvt->pasynUser, NULL) != asynSuccess)
			printf("SERVER DISCONNECTED\n");
		return asynError;
	}
	unsigned_int_32_value ui32v;

	ui32v.ui32value=0;
	ui32v.vvalue[0] = val[0];
	
	*value = (epicsInt32) ui32v.ui32value;
	free(val);	
	return asynSuccess;
}

static asynInt32 int32Methods = { int32Write, int32Read };

/*
 * asynFloat64 methods
 */
static asynStatus
float64Write(void *pvt, asynUser *pasynUser, epicsFloat64 value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;
	struct sllp_var_info * var = &ppvt->vars->list[pasynUser->reason];

    #ifdef BPM
	double_value dv;
	dv.dvalue = (double) value;
	if(sllp_write_var(ppvt->sllp, var, dv.vvalue)!=SLLP_SUCCESS)
	{
    		if( pasynOctetSyncIO->connect(ppvt->serverAddress, -1, &ppvt->pasynUser, NULL) != asynSuccess)
			printf("SERVER DISCONNECTED\n");
		return asynError;
	}
    #elif defined PUC
    unsigned int raw = (unsigned int) (((double)value+10)*262143)/20.0;
    int i;
    uint8_t* buf;
    buf = (uint8_t*) malloc(3*sizeof(char));

    for(i = 2; i >= 0; i--)
    {
        buf[i] = raw & 0xFF;
        raw >> 8;
    }

    if(sllp_write_var(ppvt->sllp, var, buf) != SLLP_SUCCESS) return asynError;
    free(buf);
    #endif

	return asynSuccess;
}

static asynStatus
float64Read(void *pvt, asynUser *pasynUser, epicsFloat64 *value)
{
	FrontendPvt *ppvt = (FrontendPvt *)pvt;

	struct sllp_var_info * var = &ppvt->vars->list[pasynUser->reason];

	#ifdef BPM
	double_value dn;
	if(sllp_read_var(ppvt->sllp, var, dn.vvalue)!=SLLP_SUCCESS)
	{
    		if( pasynOctetSyncIO->connect(ppvt->serverAddress, -1, &ppvt->pasynUser, NULL) != asynSuccess)
			printf("SERVER DISCONNECTED\n");
		return asynError;
	}
	*value = (epicsFloat64) dn.dvalue;

	#elif defined PUC
	int i;
	uint8_t *val;
	unsigned int raw = 0;
	val = (uint8_t*) malloc(3*sizeof(char));
    sllp_read_var(ppvt->sllp, var, val);
	for(i=0; i < 3; i++)
	{
        raw = raw << 8;
		raw += val[i];			
	}

    printf("Raw = %u\n", raw);

	//18 bits
	float result = ((20*raw)/262143.0)-10;
    printf("Result = %f\n", result);

	*value = (epicsFloat64) result;
	free(val);
	#endif
	return asynSuccess;
}

static asynFloat64 float64Methods = { float64Write, float64Read };

epicsShareFunc int 
//devFrontendConfigure(const char *portName, const char *hostInfo, int flags, int priority)
devFrontendConfigure(const char *portName, const char *hostInfo, int priority)
{
    FrontendPvt *ppvt;
    char *lowerName, *host;
    asynStatus status;

    #ifdef DEBUG
    printf("Configuration initiated\n");
    #endif
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
    ppvt->serverAddress = lowerName;
    if (status != asynSuccess) {
        printf("Can't connect to \"%s\"\n", lowerName);
        return -1;
    }
    free(host);
    free(lowerName);

    //TODO:remove!
    setEpicsuser(ppvt->pasynUser);

    #ifdef BPM
    printf("BPM\n");
    ppvt->sllp = sllp_client_new(sendBPM, recvCommandEPICS);
    #elif defined PUC
    printf("PUC\n");
    ppvt->sllp = sllp_client_new(sendPUC, recvCommandEPICS);
    #endif

    //ppvt->sllp = sllp_client_new(sendCommandepics, recvCommandepics);
    
    if (!ppvt->sllp)
    {
        printf("SLLP fail\n");
        return -1;
    }
    enum sllp_err err;
    if (err = sllp_client_init(ppvt->sllp)!=SLLP_SUCCESS){
	printf("Client initialization error: %d\n",err);
        return asynError;
    }

    if (sllp_get_vars_list(ppvt->sllp, &ppvt->vars)!=SLLP_SUCCESS){
        printf("Variable listing error\n");
    }

    #ifdef DEBUG
    printf("SLLP initialized\n");
    #endif


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
    
    ppvt->asynDrvUser.interfaceType = asynDrvUserType;
    ppvt->asynDrvUser.pinterface = &drvUser;
    ppvt->asynDrvUser.drvPvt = ppvt;
    status = pasynManager->registerInterface(portName, &ppvt->asynDrvUser);
    if (status != asynSuccess) {
        printf("Can't register asynFloat64 support.\n");
        return -1;
    }

    #ifdef DEBUG
    printf("Configuration succeeded\n");
    #endif
    
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
