#include <stdlib.h>
#include <stdio.h>
#include <epicsString.h>
#include <epicsStdio.h>
#include "asynDriver.h"
/** Number of asyn parameters (asyn commands) this driver supports. */
#define FRONTEND_N_PARAMS 9

/** Specific asyn commands for this support module. These will be used and
 * managed by the parameter library (part of areaDetector). */
typedef enum FrontendParam_t {
	t_setpoint1         /** Temperature setpoint*/,
	t_setpoint2         /** Temperature setpoint*/,
	t_setpoint3         /** Temperature setpoint*/,
	t_setpoint4         /** Temperature setpoint*/,
	t_sensor1   /** Temeperature sensor 1*/,	
	t_sensor2 /** Temperature sensor 2*/,	
	t_sensor3 /** Temperature sensor 3*/,	
	t_sensor4    /** Temeperature sensor 4*/,	
	c1_switchstate      /** Switch state*/,
				
    FrontendLastParam
} FrontendParam_t;

typedef struct{
	FrontendParam_t paramEnum;
	char *paramString;
}FrontendParamStruct;

static FrontendParamStruct FrontendParam[FRONTEND_N_PARAMS] = {
	{t_setpoint1,      "T_SetPoint1"},
	{t_setpoint2,      "T_SetPoint2"},
	{t_setpoint3,      "T_SetPoint3"},
	{t_setpoint4,      "T_SetPoint4"},
	{t_sensor1,         "T_Sensor1"},
	{t_sensor2,  "T_Sensor2"},
	{t_sensor3,   "T_Sensor3"},
	{t_sensor4, "T_Sensor4"},
	{c1_switchstate, "S_State"},
};

asynStatus frontendparamProcess(asynUser *pasynUser, char *pstring, const char *drvInfo,const char **pptypeName, size_t *psize);
