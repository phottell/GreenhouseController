/*
 * main.c
 * 
 * Copyright 2015  <pi@raspberrypi>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <bcm2835.h>
#include "pi_2_dht_read.h"
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
 
#define FAN1_PIN RPI_GPIO_P1_11
#define FAN2_PIN RPI_BPLUS_GPIO_J8_35
#define FAN3_PIN RPI_V2_GPIO_P1_13
#define HEATER_PIN RPI_V2_GPIO_P1_15
#define WATER_SOLENOID_PIN RPI_V2_GPIO_P1_16
#define WINDOW_RED RPI_V2_GPIO_P1_18
#define WINDOW_BLACK RPI_V2_GPIO_P1_22
#define WINDOW_STOP RPI_BPLUS_GPIO_J8_29
#define LIGHTS_OVERHEAD RPI_BPLUS_GPIO_J8_31
#define ADC0832_CS RPI_BPLUS_GPIO_J8_33
#define ADC0832_CLK RPI_BPLUS_GPIO_J8_36
#define ADC0832_MISO RPI_BPLUS_GPIO_J8_35
#define GROW_LIGHT_BLUE_PWM_PIN RPI_GPIO_P1_12
#define GROW_LIGHT_RED_PIN RPI_BPLUS_GPIO_J8_36
#define INTERNAL_FAN RPI_BPLUS_GPIO_J8_29
#define PWM_CHANNEL 0
#define RANGE 1024

#define OPEN 1 
#define CLOSED 0

uint8_t LightsState = 0;
uint8_t GrowLightOverride = 0;
uint8_t GrowLightState = 0;
uint8_t WindowOverride = 0;
uint8_t Fan1Override = 0;
uint8_t Fan2Override = 0;
uint8_t Fan3Override = 0;
uint8_t HeatOverride = 0;
uint8_t MistOverride = 0;
float CurrentHumidity;
float CurrentTemperature;
int CurrentLightVal;

FILE *EventFile;
char EventMessage[80];

FILE *Fan1StatusFile;
char fan1statusfilename[30];
FILE *Fan2StatusFile;
char fan2statusfilename[30];
FILE *Fan3StatusFile;
char fan3statusfilename[30];
FILE *HeaterStatusFile;
char heaterstatusfilename[30];
FILE *MistStatusFile;
char miststatusfilename[30];
FILE *OverheadLightsStatusFile;
char overheadlightsstatusfilename[30];
FILE *GrowLightsStatusFile;
char growlightsstatusfilename[30];
FILE *WindowsStatusFile;
char windowsstatusfilename[30];

void LogEvent(char* msg);

void OpenWindows()
{

	LogEvent("Opened Windows");
	bcm2835_gpio_write(WINDOW_BLACK, HIGH);
	bcm2835_gpio_write(WINDOW_RED, LOW);
	WindowsStatusFile = fopen(windowsstatusfilename, "w+");
	
	if(WindowsStatusFile != NULL)
	{	
		fputs("Open", WindowsStatusFile);
		fclose(WindowsStatusFile);
	}
}

void CloseWindows()
{
	LogEvent("Closed Windows");
	bcm2835_gpio_write(WINDOW_BLACK, LOW);
	bcm2835_gpio_write(WINDOW_RED, HIGH);
	WindowsStatusFile = fopen(windowsstatusfilename, "w+");
	
	if(WindowsStatusFile != NULL)
	{
		fputs("Closed", WindowsStatusFile);
		fclose(WindowsStatusFile);
	}
}

void StopWindows()
{
	//LogEvent("Stopped Windows");
	bcm2835_gpio_write(WINDOW_BLACK, HIGH);
	bcm2835_gpio_write(WINDOW_RED, HIGH);
}

int WindowState()
{
	uint8_t black_state, red_state;
	
	black_state = bcm2835_gpio_lev(WINDOW_BLACK);
	red_state = bcm2835_gpio_lev(WINDOW_RED);
	
	if((black_state == 1) && (red_state == 0))
	{
		return OPEN;
	}
	else if((black_state == 0) && (red_state == 1))
	{
		return CLOSED;
	}
	else return -1;
	
}

void LogEvent(char* msg)
{
	const char *filename = "/home/pi/EventLog.txt";
	time_t thetime;
	struct tm *p;
	char timestamp[30];
	
	thetime = time(NULL);
	p = localtime(&thetime);
	strcpy(timestamp, ctime(&thetime));
	timestamp[strlen(timestamp) - 1] = '\0';
	
	EventFile = fopen(filename, "a+");
	
	if(EventFile != NULL)
	{
		sprintf(EventMessage, "%s - %s\n", timestamp, msg);
		fwrite(EventMessage, sizeof(unsigned char), strlen(EventMessage), EventFile); 
		fclose(EventFile);
		EventFile = NULL;
	}
	else // Not found
	{
		EventFile = fopen(filename, "wb");
		if(EventFile != NULL)
		{
			sprintf(EventMessage, "%s - %s\n", ctime(&thetime), msg);
			fwrite(EventMessage, sizeof(unsigned char), strlen(EventMessage), EventFile); 
			fclose(EventFile);
			EventFile = NULL;
		}
	}
}

void DataLogFileThread()
{	
	time_t thetime;
	struct tm *p;
	char timestamp[30];
	uint8_t day = 0xff;
	FILE *DataFile;
	char datestr[30];
	char filename[80];
	uint8_t lastMin = 0xff;
		
	while(1)
	{
		thetime = time(NULL);
		p = localtime(&thetime);

		if(p->tm_mday != day)
		{
			// Create new log file for each day
			strftime(datestr, sizeof(datestr), "%Y%m%d", p);
			sprintf(filename, "/home/pi/projects/GreenhouseController/Logs/DataLog_%s.xml", datestr);
			DataFile = fopen(filename, "r+");
			
			if(DataFile != NULL) // File exists
			{
				//fclose(DataFile);
				//DataFile = NULL; 
			}
			else
			{
				DataFile = fopen(filename, "w+"); // Create the file
			
				if(DataFile != NULL)
				{
					fwrite("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\r\n", sizeof(unsigned char), 45, DataFile);
					fwrite("<HSCS>\r\n", sizeof(unsigned char), 8, DataFile);
					fwrite("</HSCS>\r\n", sizeof(unsigned char), 9, DataFile);
					fclose(DataFile);
					DataFile = NULL;
				}
			}
		}
		
		// New entry if time window has elapsed
		if(((p->tm_min % 1 == 0)) && (p->tm_min != lastMin))
		{
			char buf[40];
			DataFile = fopen(filename, "r+");
			
			if(DataFile != NULL)
			{
				strcpy(timestamp, ctime(&thetime));
				timestamp[strlen(timestamp) - 1] = '\0';

				fseek(DataFile, -9, SEEK_END);
				fwrite("\t<Data>\r\n", sizeof(unsigned char), 9, DataFile);
				sprintf(buf, "\t\t<Temp>%f</Temp>\r\n\0", CurrentTemperature);
				fwrite(buf, sizeof(unsigned char), strlen(buf), DataFile);
				sprintf(buf, "\t\t<Humidity>%f</Humidity>\r\n\0", CurrentHumidity);
				fwrite(buf, sizeof(unsigned char), strlen(buf), DataFile);
				sprintf(buf, "\t\t<Light>%d</Light>\r\n\0", CurrentLightVal);
				fwrite(buf, sizeof(unsigned char), strlen(buf), DataFile);
				sprintf(buf, "\t\t<Time>%d:%02d</Time>\r\n\0", p->tm_hour, p->tm_min);
				fwrite(buf, sizeof(unsigned char), strlen(buf), DataFile);
				
				fwrite("\t</Data>\r\n", sizeof(unsigned char), 10, DataFile);
				fwrite("</HSCS>\r\n", sizeof(unsigned char), 9, DataFile);
				fclose(DataFile);
				DataFile = NULL;
			}
			lastMin = p->tm_min;
		}
		day = p->tm_mday;
		
		bcm2835_delay(100);
	}
	
}

float FindAvgTemp(float* temps)
{
	int index;
	float Average = 0;
	float Sum = 0;
	int count = 0;
	
	for(index = 0; index < 5; index++)
	{
		if(temps[index] != -1)
		{
				Sum += temps[index];
				count++;
		}
	}
	Average = Sum / count;
}

void PollTempHumidity()
{
	float humid;
	float temp;
	float internal_humid;
	float internal_temp;
	int fan1On,fan2On, fan3On, heaterOn, MisterOn = 0, WhiteLEDsOn = 0, GrowLEDsOn = 0;
	float temp_avg[5] = {-1, -1, -1, -1, -1};
	float AvgTemp;
	int avg_index = 0;
	char avgtempfilename[30];
	char humidityfilename[30];
	FILE *AvgTempFile;
	FILE *HumidityFile;
	char strAvgTemp[5];
	char strHumidity[5];
	int alarmSent = 0;
	
	sprintf(avgtempfilename, "/dev/shm/AvgTemp");
	AvgTempFile = fopen(avgtempfilename, "r+");
	
	if(AvgTempFile != NULL) // File exists
	{
		//fclose(DataFile);
		//DataFile = NULL; 
	}
	else
	{
		AvgTempFile = fopen(avgtempfilename, "w+"); // Create the file
			
		if(AvgTempFile != NULL) // File exists
		{
			fclose(AvgTempFile);
		}
	}
	
	sprintf(humidityfilename, "/dev/shm/Humidity");
	HumidityFile = fopen(humidityfilename, "r+");
	
	if(HumidityFile != NULL) // File exists
	{
		//fclose(DataFile);
		//DataFile = NULL; 
	}
	else
	{
		HumidityFile = fopen(humidityfilename, "w+"); // Create the file
		if(HumidityFile != NULL)
		{
			fclose(HumidityFile);
		}
	}

	while(1)
	{
		while(pi_2_dht_read(DHT22, 4, &humid, &temp) != DHT_SUCCESS)
		{
			bcm2835_delay(500);	// Retry every 1/2 sec until successful read
		}
		
		temp = ((temp * 9) / 5) + 32; // Convert from C to F
		printf("Temperature = %f, humidity = %f\n", temp, humid);
		CurrentTemperature = temp;
		CurrentHumidity = humid;
		
		temp_avg[avg_index] = temp;
		avg_index++;
		if(avg_index == 5)
		{
			avg_index = 0;
		}
		
		AvgTemp = FindAvgTemp(temp_avg); 
		
		if(AvgTemp < 50)
		{
			if(alarmSent == 0) 
			{
				system("./notify_low_temp.sh");
				alarmSent = 1;
			}
		}else if (AvgTemp > 60) 
		{
			alarmSent = 0;
		}
		
		int x = WindowState();
		
		// write avg temp to RAMDisk file
		AvgTempFile = fopen(avgtempfilename, "r+");
		if(AvgTempFile != NULL)
		{
			sprintf(strAvgTemp, "%.1f", AvgTemp);
			fwrite(strAvgTemp, sizeof(unsigned char), strlen(strAvgTemp), AvgTempFile);
			fclose(AvgTempFile);
		}
			
		HumidityFile = fopen(humidityfilename, "r+");
		if(HumidityFile != NULL)
		{
			sprintf(strHumidity, "%.1f", CurrentHumidity);
			fwrite(strHumidity, sizeof(unsigned char), strlen(strHumidity), HumidityFile);
			fclose(HumidityFile);
		}
			
		if(AvgTemp > 90) // Need hysteresis loop
		{
			if(Fan1Override == 0)
			{
				if(bcm2835_gpio_lev(FAN1_PIN) == HIGH)
				{
					bcm2835_gpio_write(FAN1_PIN, LOW);
					fan1On = 1;
					Fan1StatusFile = fopen(fan1statusfilename, "w+");
					if(Fan1StatusFile != NULL)
					{
						fputs("On", Fan1StatusFile);
						fclose(Fan1StatusFile);
					}
					LogEvent("Turned Fan 1 On");
				}
			}
			// Open the windows
			if((WindowOverride == 0) && (WindowState() != OPEN))
			{
				OpenWindows();
			}
			
			if(AvgTemp > 95)
			{
				if(Fan2Override == 0)
				{
					if(bcm2835_gpio_lev(FAN2_PIN) == HIGH)
					{
						bcm2835_gpio_write(FAN2_PIN, LOW);
						fan2On = 1;
						Fan2StatusFile = fopen(fan2statusfilename, "w+");
						if(Fan2StatusFile != NULL)
						{
							fputs("On", Fan2StatusFile);
							fclose(Fan2StatusFile);
						}
						LogEvent("Turned Fan 2 On");
					}
				}
			}
			else 
			{
				if(Fan2Override == 0)
				{
					if(bcm2835_gpio_lev(FAN2_PIN) == LOW)
					{
						bcm2835_gpio_write(FAN2_PIN, HIGH);
						fan2On = 0;
						Fan2StatusFile = fopen(fan2statusfilename, "w+");
						if(Fan2StatusFile != NULL)
						{
							fputs("Off", Fan2StatusFile);
							fclose(Fan2StatusFile);
						}
						LogEvent("Turned Fan 2 Off");
					}
				}
			}
			
			if(AvgTemp > 100)
			{
				if(Fan3Override == 0)
				{
					if(bcm2835_gpio_lev(FAN3_PIN) == HIGH)
					{
						bcm2835_gpio_write(FAN3_PIN, LOW);
						fan3On = 1;
						Fan3StatusFile = fopen(fan3statusfilename, "w+");
						if(Fan3StatusFile != NULL)
						{
							fputs("On", Fan3StatusFile);
							fclose(Fan3StatusFile);
						}
						LogEvent("Turned Fan 3 On");
					}
				}
			}
			else
			{
				if(Fan3Override == 0)
				{
					if(bcm2835_gpio_lev(FAN3_PIN) == LOW)
					{
						bcm2835_gpio_write(FAN3_PIN, HIGH);
						fan3On = 0;
						Fan3StatusFile = fopen(fan3statusfilename, "w+");
						if(Fan3StatusFile != NULL)
						{
							fputs("Off", Fan3StatusFile);
							fclose(Fan3StatusFile);
						}
						LogEvent("Turned Fan 3 Off");
					}
				}
			}
			
		}
		else if(AvgTemp < 88)// below 88
		{
			if((WindowOverride == 0)  && (WindowState() != CLOSED))
			{
				CloseWindows(); 
			}
			
			if(Fan1Override == 0 )
			{
				if(bcm2835_gpio_lev(FAN1_PIN) == LOW)
				{
					bcm2835_gpio_write(FAN1_PIN, HIGH);
					fan1On = 0;
					Fan1StatusFile = fopen(fan1statusfilename, "w+");
					if(Fan1StatusFile != NULL)
					{
						fputs("Off", Fan1StatusFile);
						fclose(Fan1StatusFile);
					}
					LogEvent("Turned Fan 1 Off");
				}
			}
			if(Fan2Override == 0)
			{
				if(bcm2835_gpio_lev(FAN2_PIN) == LOW)
				{
					bcm2835_gpio_write(FAN2_PIN, HIGH);
					fan2On = 0;
					Fan2StatusFile = fopen(fan2statusfilename, "w+");
					if(Fan2StatusFile != NULL)
					{
						fputs("Off", Fan2StatusFile);
						fclose(Fan2StatusFile);
					}
					LogEvent("Turned Fan 2 Off");
				}
			}
			if(Fan3Override == 0)
				{
					if(bcm2835_gpio_lev(FAN3_PIN) == LOW)
					{
						bcm2835_gpio_write(FAN3_PIN, HIGH);
						fan3On = 0;
						Fan3StatusFile = fopen(fan3statusfilename, "w+");
						if(Fan3StatusFile != NULL)
						{
							fputs("Off", Fan3StatusFile);
							fclose(Fan3StatusFile);
						}
						LogEvent("Turned Fan 3 Off");
					}
				}
		}
		
		if((AvgTemp > 105) || (humid < 20))
		{
			if(MistOverride == 0)
			{
				if(bcm2835_gpio_lev(WATER_SOLENOID_PIN) == HIGH)
				{
					bcm2835_gpio_write(WATER_SOLENOID_PIN, LOW); // Turn water mister on and set timer
					MisterOn = 1;
					MistStatusFile = fopen(miststatusfilename, "w+");
					if(MistStatusFile != NULL)
					{
						fputs("On", MistStatusFile);
						fclose(MistStatusFile);
					}
					LogEvent("Turned Mist On");
					//bcm2835_gpio_write(FAN1_PIN, HIGH);
					//LogEvent("Turned Fan ! Off");
					//bcm2835_gpio_write(FAN2_PIN, HIGH);
					//LogEvent("Turned Fan 2 Off");
					//bcm2835_gpio_write(FAN3_PIN, HIGH);
					//LogEvent("Turned Fan 3 Off");
					//bcm2835_delay(600000); // Leave mist on and fans off for 10 minutes, then resume
				}
			}
		}
		else
		{
			if(MistOverride == 0)
			{
				if(bcm2835_gpio_lev(WATER_SOLENOID_PIN) == LOW)
				{
					bcm2835_gpio_write(WATER_SOLENOID_PIN, HIGH); 
					MisterOn = 0;
					MistStatusFile = fopen(miststatusfilename, "w+");
					if(MistStatusFile != NULL)
					{
						fputs("Off", MistStatusFile);
						fclose(MistStatusFile);
					}
					LogEvent("Turned Mist Off");
				}
			}
		}

		if(AvgTemp < 75)
		{
			if(HeatOverride == 0)
			{
				if(bcm2835_gpio_lev(HEATER_PIN) == HIGH)
				{
					bcm2835_gpio_write(HEATER_PIN, LOW);
					heaterOn = 1;
					HeaterStatusFile = fopen(heaterstatusfilename, "w+");
					if(HeaterStatusFile != NULL)
					{
						fputs("On", HeaterStatusFile);
						fclose(HeaterStatusFile);
					}
					LogEvent("Turned Heater On");
				}
			}
		}
		else
		{
			 if(HeatOverride == 0)
			 {
				if(bcm2835_gpio_lev(HEATER_PIN) == LOW)
				{
					bcm2835_gpio_write(HEATER_PIN, HIGH);
					heaterOn = 0;
					HeaterStatusFile = fopen(heaterstatusfilename, "w+");
					if(HeaterStatusFile != NULL)
					{
						fputs("Off", HeaterStatusFile);
						fclose(HeaterStatusFile);
					}
					LogEvent("Turned Heater Off");
				}
			}
		}
	
		//int result = pi_2_dht_read(DHT11, 12, &internal_humid, &internal_temp);

		while(pi_2_dht_read(DHT11, 12, &internal_humid, &internal_temp) != DHT_SUCCESS)
		{
			bcm2835_delay(500);	// Retry every 1/2 sec until successful read
		}
	
		internal_temp = ((internal_temp * 9) / 5) + 32; // Convert from C to F
		
		printf("internal_temp = %f\n", internal_temp);
		
		if(internal_temp > 80)
		{
			if(bcm2835_gpio_lev(INTERNAL_FAN) == LOW)
			{
				bcm2835_gpio_write(INTERNAL_FAN, HIGH);
				LogEvent("Turned Internal Fan On");
			}
		}
		else
		{
			if(bcm2835_gpio_lev(INTERNAL_FAN) == HIGH)
			{
				bcm2835_gpio_write(INTERNAL_FAN, LOW);	
				LogEvent("Turned Internal Fan Off");
			}
		}
			
		bcm2835_delay(30000);
	}
}

void PollLightSensor()
{
	FILE *IntensityFile;
	char intensityfilename[30];
	char strIntensity[5];

	sprintf(intensityfilename, "/dev/shm/Intensity");
	
	IntensityFile = fopen(intensityfilename, "w+"); // Create the file
	fclose(IntensityFile);


	while(1)
	{
		//Set with CS pin to use for next transfers
		bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
	
		//Transfer many bytes
		char data_buffer[3];
		int Count;
		int LightVal;
		
		data_buffer[0] = 0xC0;
		data_buffer[1] = 0x00;
		bcm2835_spi_transfern(&data_buffer[0], 3);			//data_buffer used for tx and rx
		LightVal = 2048 - ((data_buffer[0] << 8) + data_buffer[1]);
		CurrentLightVal = LightVal;
		
//		printf("data_buffer[0] = 0x%x, data_buffer[1] = 0x%x,data_buffer[2] = 0x%x\n", data_buffer[0], data_buffer[1], data_buffer[2]);
		printf("Light intensity = %d\n", LightVal);

		IntensityFile = fopen(intensityfilename, "w");
		if(IntensityFile != NULL)
		{
			sprintf(strIntensity, "%d", CurrentLightVal);
			fwrite(strIntensity, sizeof(unsigned char), strlen(strIntensity), IntensityFile);
			fclose(IntensityFile);
		}
		
		if(GrowLightOverride == 0)
		{
			// TODO: Need hysteresis
			if((LightVal < 1000) && (GrowLightState == 0))
			{
				if(GrowLightState == 0)
				{
					bcm2835_pwm_set_data(PWM_CHANNEL, 204);
					bcm2835_gpio_write(GROW_LIGHT_RED_PIN, HIGH);
					GrowLightState = 1;
					GrowLightsStatusFile = fopen(growlightsstatusfilename, "w+");
					if(GrowLightsStatusFile != NULL)
					{
						fputs("On", GrowLightsStatusFile);
						fclose(GrowLightsStatusFile);
					}
					LogEvent("Turned Grow Lights On");
				}
			}
			else if(LightVal > 1500)
			{
				if(GrowLightState == 1)
				{
					bcm2835_pwm_set_data(PWM_CHANNEL, 0);
					bcm2835_gpio_write(GROW_LIGHT_RED_PIN, LOW);
					GrowLightState = 0;
					GrowLightsStatusFile = fopen(growlightsstatusfilename, "w+");
					if(GrowLightsStatusFile != NULL)
					{
						fputs("Off", GrowLightsStatusFile);
						fclose(GrowLightsStatusFile);
					}
					LogEvent("Turned Grow Lights Off");
				}
			}
		}
		bcm2835_delay(60000);
	}
}

void PollPushButtons()
{
	char bufOut[1];
	char bufIn[1];
	int col, row;
	char val;
	uint8_t OverheadLightOn = 0;
	uint8_t Fan1OnOff = 0xff;
	uint8_t Fan2OnOff = 0xff;
	uint8_t Fan3OnOff = 0xff;
	uint8_t HeaterOnOff = 0xff;
	uint8_t MistOnOff = 0xff;
	uint8_t ButtonPressed = 0;
	uint8_t ButNum = 0;
	uint8_t state;
	uint8_t old_state_11, old_state_12, old_state_21, old_state_22, old_state_24, old_state_28, old_state_41;
	
	while(1)
	{
		for(col = 1; col < 9; col *= 2)
		{ 
			bufOut[0] = 0xff & ~col; // Set column low
			bcm2835_i2c_write(bufOut, 1);
			bcm2835_i2c_read(bufIn, 1);
			val = ((bufIn[0] >> 4) & 0xf);
						
			for(row = 1; row < 9; row *= 2)
			{
				ButtonPressed = (~val & row) == 0 ? 0:1;
	
				ButNum = ((col << 4) | row);
				
				switch(ButNum)
				{
				
					case 0x11: // Column 1, Row 1 - Overhead light
						
						if(ButtonPressed != old_state_11)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 1 Row 1 pressed\n");
								if(OverheadLightOn == 0)
								{
									OverheadLightOn = 1;
									OverheadLightsStatusFile = fopen(overheadlightsstatusfilename, "w+");
									if(OverheadLightsStatusFile != NULL)
									{
										fputs("On", OverheadLightsStatusFile);
										fclose(OverheadLightsStatusFile);
									}
									LogEvent("Turned Lights On");
								}
								else if(OverheadLightOn == 1) 
								{
									OverheadLightOn = 0;
									OverheadLightsStatusFile = fopen(overheadlightsstatusfilename, "w+");
									{
										fputs("Off", OverheadLightsStatusFile);
										fclose(OverheadLightsStatusFile);
									}
									LogEvent("Turned Grow Lights Off");
								}
			
								if(OverheadLightOn == 1) 
								{
									bcm2835_gpio_write(LIGHTS_OVERHEAD, HIGH);
								}
								else 
								{
									bcm2835_gpio_write(LIGHTS_OVERHEAD, LOW);
								}
							} 
						}
						old_state_11 = ButtonPressed;
						
					break;
					
					case 0x21: // Column 2, Row 1 - Grow light override

						if(ButtonPressed != old_state_12)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 2 Row 1 pressed\n");
								
								if(GrowLightOverride == 0)
								{
									if(GrowLightState == 0) // Currently off
									{
										bcm2835_pwm_set_data(PWM_CHANNEL, 204);
										bcm2835_gpio_write(GROW_LIGHT_RED_PIN, HIGH);
										GrowLightsStatusFile = fopen(growlightsstatusfilename, "w+");
										if(GrowLightsStatusFile != NULL)
										{
											fputs("On", GrowLightsStatusFile);
											fclose(GrowLightsStatusFile);
										}
										LogEvent("Turned Grow Lights On (Override)");
									}
									else // Currently On
									{ 
										bcm2835_pwm_set_data(PWM_CHANNEL, 0);
										bcm2835_gpio_write(GROW_LIGHT_RED_PIN, LOW);	
										GrowLightsStatusFile = fopen(growlightsstatusfilename, "w+");
										if(GrowLightsStatusFile != NULL)
										{
											fputs("Off", GrowLightsStatusFile);
											fclose(GrowLightsStatusFile);
										}
										LogEvent("Turned Grow Lights Off (Override)");								
									}
									GrowLightOverride = 1;
								}
								else
								{
									GrowLightOverride = 0;
								}
							}
						}
						old_state_12 = ButtonPressed;
					break;
					
					case 0x41: // Column 3, Row 1 - Window Up
						
						if((ButtonPressed == 1) && ((val & 0xc) != 0))
						{
							printf("Col 3 Row 1 pressed\n");
							WindowOverride = 1;
							OpenWindows();
						}	
						else
						{
							if(WindowOverride == 1)
							{
								StopWindows();
							}
						}	

					break;
					
					case 0x81: // Column 4, Row 1 - Window Down

						if(ButtonPressed == 1)
						{
							printf("Col 4 Row 1 pressed\n");
							WindowOverride = 2;
							CloseWindows();
							
							// If both Window button pressed, return to auto
							if((val & 0xc) == 0)
							{
								WindowOverride = 0;
							}
							
						}	
						else
						{
							if(WindowOverride == 2)
							{
								StopWindows();
							}
						}	

					break;
					
					case 0x12: // Column 1, Row 2 - Fan 1 override
						
						if(ButtonPressed != old_state_21)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 1 Row 2 pressed\n");
								if(Fan1OnOff == 1)
								{
									Fan1Override = 0;
									bcm2835_gpio_write(FAN1_PIN, HIGH);
									Fan1OnOff = 0;
									Fan1StatusFile = fopen(fan1statusfilename, "w+");
									if(Fan1StatusFile != NULL)
									{
										fputs("Off", Fan1StatusFile);
										fclose(Fan1StatusFile);
									}
									LogEvent("Turned Fan 1 Off (Override)");
								}
								else 
								{
									Fan1Override = 1;
									bcm2835_gpio_write(FAN1_PIN, LOW);
									Fan1OnOff = 1;
									Fan1StatusFile = fopen(fan1statusfilename, "w+");
									if(Fan1StatusFile != NULL)
									{
										fputs("On", Fan1StatusFile);
										fclose(Fan1StatusFile);
									}
									
									LogEvent("Turned Fan 1 On (Override)");
								}
							}
						}
						old_state_21 = ButtonPressed;

					break;
					
					case 0x22: // Column 2, Row 2 - Fan 2 override
							
						if(ButtonPressed != old_state_22)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 2 Row 2 pressed\n");
								if(Fan2OnOff == 1)
								{
									Fan2Override = 0;
									bcm2835_gpio_write(FAN2_PIN, HIGH);
									Fan2OnOff = 0;
									Fan2StatusFile = fopen(fan2statusfilename, "w+");
									if(Fan2StatusFile != NULL)
									{
										fputs("Off", Fan2StatusFile);
										fclose(Fan2StatusFile);
									}
									LogEvent("Turned Fan 2 Off (Override)");
								}
								else 
								{
									Fan2Override = 1;
									bcm2835_gpio_write(FAN2_PIN, LOW);
									Fan2OnOff = 1;
									Fan2StatusFile = fopen(fan2statusfilename, "w+");
									if(Fan2StatusFile != NULL)
									{
										fputs("On", Fan2StatusFile);
										fclose(Fan2StatusFile);
									}
									LogEvent("Turned Fan 2 On (Override)");
								}
							}
						}
						old_state_22 = ButtonPressed;

					break;
					
					case 0x42: // Column 3, Row 2 - Fan 3 override

						if(ButtonPressed != old_state_24)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 3 Row 2 pressed\n");
								if(Fan3OnOff == 1)
								{
									Fan3Override = 0;
									bcm2835_gpio_write(FAN3_PIN, HIGH);
									Fan3OnOff = 0;
									Fan3StatusFile = fopen(fan3statusfilename, "w+");
									if(Fan3StatusFile != NULL)
									{
										fputs("Off", Fan3StatusFile);
										fclose(Fan3StatusFile);
									}
									LogEvent("Turned Fan 3 Off (Override)");
								}
								else 
								{
									Fan3Override = 1;
									bcm2835_gpio_write(FAN3_PIN, LOW);
									Fan3OnOff = 1;
									Fan3StatusFile = fopen(fan3statusfilename, "w+");
									if(Fan3StatusFile != NULL)
									{
										fputs("On", Fan3StatusFile);
										fclose(Fan3StatusFile);
									}
									LogEvent("Turned Fan 3 On (Override)");
								}
							}
						}
						old_state_24 = ButtonPressed;

					break;
					
					case 0x82: // Column 4, Row 2 - Heater override

						if(ButtonPressed != old_state_28)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 4 Row 2 pressed\n");
								if(HeaterOnOff == 1)
								{
									HeatOverride = 0;
									bcm2835_gpio_write(HEATER_PIN, HIGH);
									HeaterOnOff = 0;
									HeaterStatusFile = fopen(heaterstatusfilename, "w+");
									if(HeaterStatusFile != NULL)
									{
										fputs("Off", HeaterStatusFile);
										fclose(HeaterStatusFile);
									}
									LogEvent("Turned Heater Off (Override)");
								}
								else 
								{
									HeatOverride = 1;
									bcm2835_gpio_write(HEATER_PIN, LOW);
									HeaterOnOff = 1;
									HeaterStatusFile = fopen(heaterstatusfilename, "w+");
									if(HeaterStatusFile != NULL)
									{
										fputs("On", HeaterStatusFile);
										fclose(HeaterStatusFile);
									}
									LogEvent("Turned Heater On (Override)");
								}
							}
						}
						old_state_28 = ButtonPressed;

					break;
					
					case 0x14: // Column 1, Row 3 - Mist override

						if(ButtonPressed != old_state_41)
						{
							if(ButtonPressed == 1)
							{
								printf("Col 1 Row 3 pressed\n");
								
								if(MistOverride == 0)
								{
								
									if(MistOnOff == 1)
									{
										MistOverride = 0;
										bcm2835_gpio_write(WATER_SOLENOID_PIN, HIGH);
										MistOnOff = 0;
										MistStatusFile = fopen(miststatusfilename, "w+");
										if(MistStatusFile != NULL)
										{
											fputs("Off", MistStatusFile);
											fclose(MistStatusFile);
										}
										LogEvent("Turned Mist Off (Override)");
									}
									else 
									{
										MistOverride = 1;
										bcm2835_gpio_write(WATER_SOLENOID_PIN, LOW);
										MistStatusFile = fopen(miststatusfilename, "w+");
										if(MistStatusFile != NULL)
										{
											fputs("On", MistStatusFile);
											fclose(MistStatusFile);
										}
										MistOnOff = 1;
										LogEvent("Turned Mist On (Override)");
									}
								}
								else
								{
									MistOverride = 0;
								}
							}
						}
						old_state_41 = ButtonPressed;

					break;
					
					case 0x24: // Column 3, Row 2

					break;
					
					case 0x44: // Column 3, Row 3

					break;
					
					case 0x84: // Column 3, Row 4

					break;
					
					case 0x18: // Column 4, Row 1

					break;
					
					case 0x28: // Column 4, Row 2

					break;
					
					case 0x48: // Column 4, Row 3

					break;
					
					case 0x88: // Column 4, Row 4

					break;
					
					default:
						printf("Unknown key\n");
					break;	
				}
			}
		}
		
		// Check files for external changes
		char status[10];
		
		Fan1StatusFile = fopen(fan1statusfilename, "r");
		if(Fan1StatusFile != NULL)
		{
			fgets(status, 4, Fan1StatusFile);
		
			if((strcmp(status, "On") == 0) && (Fan1OnOff == 0))
			{
				Fan1Override = 1;
				bcm2835_gpio_write(FAN1_PIN, LOW);
				Fan1OnOff = 1;
				LogEvent("Turned Fan 1 On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (Fan1OnOff == 1))
			{
				Fan1Override = 0;
				bcm2835_gpio_write(FAN1_PIN, HIGH);
				Fan1OnOff = 0;
				LogEvent("Turned Fan 1 Off (Override)");
			}
			fclose(Fan1StatusFile);
			memset(status, 0, 4);
		}
		
		Fan2StatusFile = fopen(fan2statusfilename, "r");
		if(Fan2StatusFile != NULL)
		{
			fgets(status, 4, Fan2StatusFile);
			
			if((strcmp(status, "On") == 0) && (Fan2OnOff == 0))
			{
				Fan2Override = 1;
				bcm2835_gpio_write(FAN2_PIN, LOW);
				Fan2OnOff = 1;
				LogEvent("Turned Fan 2 On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (Fan2OnOff == 1))
			{
				Fan2Override = 0;
				bcm2835_gpio_write(FAN2_PIN, HIGH);
				Fan2OnOff = 0;
				LogEvent("Turned Fan 2 Off (Override)");
			}
			fclose(Fan2StatusFile);
			memset(status, 0, 4);
		}
		
		Fan3StatusFile = fopen(fan3statusfilename, "r");
		if(Fan3StatusFile != NULL)
		{
			fgets(status, 4, Fan3StatusFile);
			
			if((strcmp(status, "On") == 0) && (Fan3OnOff == 0))
			{
				Fan3Override = 1;
				bcm2835_gpio_write(FAN3_PIN, LOW);
				Fan3OnOff = 1;
				LogEvent("Turned Fan 3 On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (Fan3OnOff == 1))
			{
				Fan3Override = 0;
				bcm2835_gpio_write(FAN3_PIN, HIGH);
				Fan3OnOff = 0;
				LogEvent("Turned Fan 3 Off (Override)");
			}
			fclose(Fan3StatusFile);
			memset(status, 0, 4);
		}
		
		HeaterStatusFile = fopen(heaterstatusfilename, "r");
		if(HeaterStatusFile != NULL)
		{
			fgets(status, 4, HeaterStatusFile);
			
			if((strcmp(status, "On") == 0) && (HeaterOnOff == 0))
			{
				HeatOverride = 1;
				bcm2835_gpio_write(HEATER_PIN, LOW);
				HeaterOnOff = 1;
				LogEvent("Turned Heater On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (HeaterOnOff == 1))
			{
				HeatOverride = 0;
				bcm2835_gpio_write(HEATER_PIN, HIGH);
				HeaterOnOff = 0;
				LogEvent("Turned Heater Off (Override)");
			}
			fclose(HeaterStatusFile);
			memset(status, 0, 4);
		}
		
		MistStatusFile = fopen(miststatusfilename, "r");
		if(MistStatusFile != NULL)
		{
			fgets(status, 4, MistStatusFile);
			
			if((strcmp(status, "On") == 0) && (MistOnOff == 0))
			{
				MistOverride = 1;
				bcm2835_gpio_write(WATER_SOLENOID_PIN, LOW);
				MistOnOff = 1;
				LogEvent("Turned Mist On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (MistOnOff == 1))
			{
				MistOverride = 0;
				bcm2835_gpio_write(WATER_SOLENOID_PIN, HIGH);
				MistOnOff = 0;
				LogEvent("Turned Mist Off (Override)");
			}
			fclose(MistStatusFile);
			memset(status, 0, 4);
		}		
		
		OverheadLightsStatusFile = fopen(overheadlightsstatusfilename, "r");
		if(OverheadLightsStatusFile != NULL)
		{
			fgets(status, 4, OverheadLightsStatusFile);
			
			if((strcmp(status, "On") == 0) && (OverheadLightOn == 0))
			{
				bcm2835_gpio_write(LIGHTS_OVERHEAD, HIGH);
				OverheadLightOn = 1;
				LogEvent("Turned Overhead Lights On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (OverheadLightOn == 1))
			{
				bcm2835_gpio_write(LIGHTS_OVERHEAD, LOW);
				OverheadLightOn = 0;
				LogEvent("Turned Overhead Light Off (Override)");
			}
			fclose(OverheadLightsStatusFile);
			memset(status, 0, 4);
		}
		
		GrowLightsStatusFile = fopen(growlightsstatusfilename, "r");
		if(GrowLightsStatusFile != NULL)
		{
			fgets(status, 4, GrowLightsStatusFile);
			
			if((strcmp(status, "On") == 0) && (GrowLightState == 0))
			{
				bcm2835_pwm_set_data(PWM_CHANNEL, 204);
				bcm2835_gpio_write(GROW_LIGHT_RED_PIN, HIGH);
				GrowLightState = 1;
				LogEvent("Turned Grow Lights On (Override)");
			}
			else if((strcmp(status, "Off") == 0) && (GrowLightState == 1))
			{
				bcm2835_pwm_set_data(PWM_CHANNEL, 0);
				bcm2835_gpio_write(GROW_LIGHT_RED_PIN, LOW);
				GrowLightState = 0;
				LogEvent("Turned Grow Lights Off (Override)");
			}
			fclose(GrowLightsStatusFile);
			memset(status, 0, 4);
		}
		
		WindowsStatusFile = fopen(windowsstatusfilename, "r");
		if(WindowsStatusFile != NULL)
		{
			fgets(status, 10, WindowsStatusFile);
			
			if((strcmp(status, "Open") == 0) && (WindowState() == CLOSED))
			{
				OpenWindows();
				WindowOverride = 1;
			}
			else if((strcmp(status, "Closed") == 0) && (WindowState() == OPEN))
			{
				CloseWindows();
				WindowOverride = 0;
			}
			fclose(WindowsStatusFile);
			memset(status, 0, 4);
		}
		
		bcm2835_delay(100);
	}
	// Set 4 GPIOs as outputs (columns), 4 as inputs (rows)
	// Set each column low, then read rows
}

int main(int argc, char **argv)
{
	pthread_t threadLSW;
	pthread_t threadTH;
	pthread_t threadLGHT;
	pthread_t threadBUTT;
	pthread_t threadDATA;
	int rc;

    if (!bcm2835_init())
	return 1;

	LogEvent("App started");

    sprintf(fan1statusfilename, "/dev/shm/Fan1");
    sprintf(fan2statusfilename, "/dev/shm/Fan2");
    sprintf(fan3statusfilename, "/dev/shm/Fan3");
    sprintf(heaterstatusfilename, "/dev/shm/Heater");
    sprintf(miststatusfilename, "/dev/shm/Mist");
    sprintf(overheadlightsstatusfilename, "/dev/shm/OverheadLights");
    sprintf(growlightsstatusfilename, "/dev/shm/GrowLights");
    sprintf(windowsstatusfilename, "/dev/shm/Windows");
    
	struct stat buf;
	
	if(stat(overheadlightsstatusfilename, &buf) != 0) // If not exist, create and set to Off
	{
		OverheadLightsStatusFile = fopen(overheadlightsstatusfilename, "w+");
		if(OverheadLightsStatusFile != NULL)
		{
			fputs("Off", OverheadLightsStatusFile);
			fclose(OverheadLightsStatusFile);
		}
	}
	
	if(stat(growlightsstatusfilename, &buf) != 0) // If not exist, create and set to Off
	{
		GrowLightsStatusFile = fopen(growlightsstatusfilename, "w+");
		if(GrowLightsStatusFile != NULL)
		{
			fputs("Off", GrowLightsStatusFile);
			fclose(GrowLightsStatusFile);
		}
	}
	
	if(stat(windowsstatusfilename, &buf) != 0)
	{
		WindowsStatusFile = fopen(windowsstatusfilename, "w+");
		if(WindowsStatusFile != NULL)
		{
			fputs("Closed", WindowsStatusFile);
			fclose(WindowsStatusFile);
		}
	}
	
	HeaterStatusFile = fopen(heaterstatusfilename, "w+");
	if(HeaterStatusFile != NULL)
	{
		fputs("Off", HeaterStatusFile);
		fclose(HeaterStatusFile);
	}
	//Setup SPI pins
	bcm2835_spi_begin();
	bcm2835_i2c_begin();
	
	bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, 0);
	bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS1, 0);

	bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_128);
	bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
	
    bcm2835_gpio_fsel(FAN1_PIN , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(FAN2_PIN , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(FAN3_PIN , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(HEATER_PIN , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(WATER_SOLENOID_PIN , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(WINDOW_BLACK , BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(WINDOW_RED , BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_fsel(LIGHTS_OVERHEAD, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_fsel(GROW_LIGHT_RED_PIN, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_fsel(INTERNAL_FAN, BCM2835_GPIO_FSEL_OUTP);
	
    // Set the output pin to Alt Fun 5, to allow PWM channel 0 to be output there
    bcm2835_gpio_fsel(GROW_LIGHT_BLUE_PWM_PIN, BCM2835_GPIO_FSEL_ALT5);
    // Clock divider is set to 16.
    // With a divider of 16 and a RANGE of 1024, in MARKSPACE mode,
    // the pulse repetition frequency will be
    // 1.2MHz/1024 = 1171.875Hz, suitable for driving a DC motor with PWM
    bcm2835_pwm_set_clock(BCM2835_PWM_CLOCK_DIVIDER_16);
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    bcm2835_pwm_set_range(PWM_CHANNEL, RANGE);
	bcm2835_pwm_set_data(PWM_CHANNEL, 0);
	bcm2835_gpio_write(GROW_LIGHT_RED_PIN, LOW);
	bcm2835_gpio_write(INTERNAL_FAN, LOW);
	
	bcm2835_i2c_setSlaveAddress(0x20);
	
	rc = pthread_create(&threadTH, NULL, (void*)&PollTempHumidity, NULL);
	rc = pthread_create(&threadLGHT, NULL, (void*)&PollLightSensor, NULL);
	rc = pthread_create(&threadBUTT, NULL, (void*)&PollPushButtons, NULL);
	bcm2835_delay(1000); //Give the PollTempHumidity thread time to update 
	rc = pthread_create(&threadDATA, NULL, (void*)&DataLogFileThread, NULL);
	
	while(1)
	{
		//TODO: Feed watchdog
		bcm2835_delay(100);
	}
	//Return SPI pins to default inputs state
	bcm2835_spi_end();

	return 0;
}
