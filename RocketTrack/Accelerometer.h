
#pragma once

extern int acc_enable;
extern char acc_type[];
extern int acc_rate;

int SetupAccelerometer(void);
void PollAccelerometer(void);

int AccelerometerCommandHandler(uint8_t *cmd,uint16_t cmdptr);

enum
{
	ACCELEROMETER_NONE=0,
	ACCELEROMETER_ADXL345,
	ACCELEROMETER_MPU6050,
	ACCELEROMETER_MPU9250
};

