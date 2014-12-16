#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <vector>
#include <math.h>

void print_time(void)
{
	time_t time_raw_format;
 	time(&time_raw_format); //获取当前时间
 	fprintf(stderr, "The current local time: %s", ctime(&time_raw_format));
 	return ;
}

float currentplaytime(AVRational time_base, int64_t pts)
{
	float basetime = pts / (time_base.den);

	float seconds = basetime * time_base.num;
//	printf("time_base den and num %d\t%d\n",time_base.den,time_base.num);
//	printf("currentplaytme seconds is %f\n",seconds);		
	return seconds;
}

int seconds2timestring(char * buf, int bufsize, float seconds)
{
	int hour = (int)(seconds / 3600);
	int minite = (int)(seconds / 60  - hour*60);
	int second = (int)(seconds - 3600*hour - 60*minite);
	char temp[10];
	memset(temp,0,10);memset(buf,0,bufsize);
	if(hour > 12)return -1;
	if(minite > 59)return -1;
	if(second > 59)return -1;
	if(hour < 10){
	sprintf(temp,"0%d:",hour);strcat(buf,temp);memset(temp,0,10);}
	else {sprintf(temp,"%d:",hour);strcat(buf,temp);memset(temp,0,10);}
	if(minite<10){
	sprintf(temp,"0%d:",minite);strcat(buf,temp);memset(temp,0,10);}
	else {sprintf(temp,"%d",minite);strcat(buf,temp);memset(temp,0,10);}
	if(second < 10){
	sprintf(temp,"0%d",second);strcat(buf,temp);}
	else {sprintf(temp,"%d",second);strcat(buf,temp);}
	return 0;
	
}

std::vector<std::string> split(std::string str, std::string pattern)
{
	std::string::size_type pos;
	std::vector<std::string> result;
	str+=pattern;
	int size = str.size();
	for(int i = 0; i<size; i++)
		{
			pos=str.find(pattern,i);
			if(pos<size)
			{
				std::string s=str.substr(i,pos-i);
				result.push_back(s);
				i=pos+pattern.size()-1;
			}
		}
	return result;
}

float time2sec(const char* time)
	{	
		std::string stime(time);
		std::string pattern(":");
		std::vector<std::string> result=split(stime,pattern);
		int size = result.size();
//		for(int k=0; k<result.size();k++)std::cout<<result[k]<<std::endl;
		float seconds;
		if(size != 3)return -1;
		seconds=atof(result[0].c_str())*3600+atof(result[1].c_str())*60+atof(result[2].c_str());
			return seconds;	
	}





