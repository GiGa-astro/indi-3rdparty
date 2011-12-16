/*******************************************************************************
  Copyright(c) 2010 Gerry Rozema. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/
#include "sxcam.h"

// We declare an auto pointer to sxcamera.
std::auto_ptr<SxCam> sxcamera(0);

void ISInit()
{
   static int isInit =0;

   if (isInit == 1)
       return;

    isInit = 1;
    if(sxcamera.get() == 0) sxcamera.reset(new SxCam());
    //IEAddTimer(POLLMS, ISPoll, NULL);

}

void ISGetProperties(const char *dev)
{
        ISInit();
        sxcamera->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int num)
{
        ISInit();
        sxcamera->ISNewSwitch(dev, name, states, names, num);
}

void ISNewText(	const char *dev, const char *name, char *texts[], char *names[], int num)
{
        ISInit();
        sxcamera->ISNewText(dev, name, texts, names, num);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int num)
{
        ISInit();
        sxcamera->ISNewNumber(dev, name, values, names, num);
}

void ISNewBLOB (const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
  INDI_UNUSED(dev);
  INDI_UNUSED(name);
  INDI_UNUSED(sizes);
  INDI_UNUSED(blobsizes);
  INDI_UNUSED(blobs);
  INDI_UNUSED(formats);
  INDI_UNUSED(names);
  INDI_UNUSED(n);
}
void ISSnoopDevice (XMLEle *root)
{
    INDI_UNUSED(root);
}

SxCam::SxCam()
{
    //ctor
    SubX=0;
    SubY=0;
    SubW=1392;
    SubH=1040;
    BinX=1;
    BinY=1;
    Interlaced=false;
    RawFrame = NULL;
    HasGuideHead=false;
    CamBits=8;
    StreamSP = new ISwitchVectorProperty;
}

SxCam::~SxCam()
{
    //dtor
    if(RawFrame != NULL) delete RawFrame;
    usb_close(usb_handle);
}

bool SxCam::initProperties()
{
    INDI::CCD::initProperties();

    /* Video Stream */
     IUFillSwitch(&StreamS[0], "ON", "Stream On", ISS_OFF);
     IUFillSwitch(&StreamS[1], "OFF", "Stream Off", ISS_ON);
     IUFillSwitchVector(StreamSP, StreamS, 2, deviceName(), "VIDEO_STREAM", "Video Stream", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
}

bool SxCam::updateProperties()
{

    INDI::CCD::updateProperties();

    if (isConnected())
        defineSwitch(StreamSP);
    else
        deleteProperty(StreamSP->name);
}

const char * SxCam::getDefaultName()
{
    return (char *)"SX CCD";
}

bool SxCam::Connect()
{
    bool rc;
    IDLog("Calling sx connect\n");
    rc=SxCCD::Connect();
    if(rc) {
        IDLog("Calling Set CCD %d %d\n",XRes,YRes);
        SetCCDParams(XRes,YRes,CamBits,pixwidth,pixheight);
        if(HasGuideHead) {
            printf("Guide head detected\n");
            SetGuidHeadParams(GXRes,GYRes,GuiderBits,gpixwidth,gpixheight);
        } else {
            printf("no guide head\n");
        }
    }
    return rc;
}

bool SxCam::Disconnect()
{
    bool rc;
    rc=SxCCD::Disconnect();
    return rc;
}

bool SxCam::ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if(strcmp(dev,deviceName())==0)
    {
        //  it's for this device

        //for(int x=0; x<n; x++) {
        //    IDLog("Switch %s\n",names[x]);
        //}

        if(strcmp(name,StreamSP->name)==0)
        {

            IUUpdateSwitch(StreamSP,states,names,n);

            if (StreamS[0].s == ISS_ON)
            {
                StreamSP->s = IPS_BUSY;
                IDSetSwitch(StreamSP, "Starting video stream.");
                StartExposure(0.5);
            }
            else
            {
                StreamSP->s = IPS_IDLE;
                IDSetSwitch(StreamSP, "Video stream stopped.");
            }

            return true;
        }
  }

    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);

}

float SxCam::CalcTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now;
    gettimeofday(&now,NULL);

    timesince=(double)(now.tv_sec * 1000.0 + now.tv_usec/1000) - (double)(ExpStart.tv_sec * 1000.0 + ExpStart.tv_usec/1000);
    timesince=timesince/1000;


    timeleft=ExposureRequest-timesince;
    return timeleft;
}
float SxCam::CalcGuideTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now;
    gettimeofday(&now,NULL);

    timesince=(double)(now.tv_sec * 1000.0 + now.tv_usec/1000) - (double)(GuideExpStart.tv_sec * 1000.0 + GuideExpStart.tv_usec/1000);
    timesince=timesince/1000;


    timeleft=GuideExposureRequest-timesince;
    return timeleft;
}

int SxCam::StartExposure(float n)
{
    ExposureRequest=n;
    gettimeofday(&ExpStart,NULL);
    InExposure=true;

    //  Clear the pixels to start a fresh exposure
    //  calling here with no parameters flushes both
    //  the accumulators and the light sensative portions
    DidFlush=0;
    DidLatch=0;

    ClearPixels(SXCCD_EXP_FLAGS_FIELD_BOTH,IMAGE_CCD);

    //  Relatively long exposure
     //  lets do it on our own timers
     int tval;
     tval=n*1000;
     tval=tval-50;
     if(tval < 1)
         tval=1;
     if(tval > 250)
         tval=250;

     IDLog("Cleared all fields, setting timer to %d\n", tval);

     SetTimer(tval);

    return 0;
}

int SxCam::StartGuideExposure(float n)
{
    GuideExposureRequest=n;

    IDLog("Start guide exposure %4.2f\n",n);

    if(InGuideExposure)
    {
        //  We already have an exposure running
        //  so we just change the exposure time
        //  and return
        return true;
    }

    gettimeofday(&GuideExpStart,NULL);
    InGuideExposure=true;

    //  Clear the pixels to start a fresh exposure
    //  calling here with no parameters flushes both
    //  the accumulators and the light sensative portions
    DidGuideLatch=0;

        //ClearPixels(SXCCD_EXP_FLAGS_FIELD_BOTH,GUIDE_CCD);
        //  Relatively long exposure
        //  lets do it on our own timers
        int tval;
        tval=n*1000;
        tval=tval-50;
        if(tval < 1) tval=1;
        if(tval > 250) tval=250;
        SetTimer(tval);

    return 0;
}

bool SxCam::AbortGuideExposure()
{
    if(InGuideExposure) {
        InGuideExposure=false;
        return true;
    }
    return false;
}


void SxCam::TimerHit()
{
    float timeleft;
    int rc;
    bool IgnoreGuider=false;

    IDLog("SXCam Timer \n");

    //  If this is a relatively long exposure
    //  and its nearing the end, but not quite there yet
    //  We want to flush the accumulators

    if(InExposure)
    {
        timeleft=CalcTimeLeft();

        if((timeleft < 3) && (timeleft > 2) && (DidFlush==0)&&(InExposure))
        {
            //  This will clear accumulators, but, not affect the
            //  light sensative parts currently exposing
            IDLog("Doing Flush\n");
            ClearPixels(SXCCD_EXP_FLAGS_NOWIPE_FRAME,IMAGE_CCD);
            DidFlush=1;
        }

        if(timeleft < 1.0)
        {
            IgnoreGuider=true;
            if(timeleft > 0.25)
            {
                //  a quarter of a second or more
                //  just set a tighter timer
                SetTimer(250);
            } else
            {
                if(timeleft >0.07)
                {
                    //  use an even tighter timer
                    SetTimer(50);
                } else
                {
                    //  it's real close now, so spin on it
                    while(timeleft > 0)
                    {
                        int slv;
                        slv=100000*timeleft;
                        //IDLog("usleep %d\n",slv);
                        usleep(slv);
                        timeleft=CalcTimeLeft();
                    }

                    if (Interlaced)
                        rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_EVEN,IMAGE_CCD,SubX,SubY,SubW,SubH,BinX,BinY);
                    else
                        rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_BOTH,IMAGE_CCD,SubX,SubY,SubW,SubH,BinX,BinY);
                           // IDLog("Image Pixels latched with rc=%d\n", rc);

                    DidLatch=1;

                }
            }
        } else
        {
            if(!InGuideExposure) SetTimer(250);
        }
    }

    if(!IgnoreGuider)
    {
        if(InGuideExposure)
        {
            timeleft=CalcGuideTimeLeft();
            if(timeleft < 0.25)
            {
                if(timeleft < 0.10)
                {
                    while(timeleft > 0)
                    {
                        int slv;
                        slv=100000*timeleft;
                        //IDLog("usleep %d\n",slv);
                        usleep(slv);
                        timeleft=CalcGuideTimeLeft();
                    }
                    //  first a flush
                    ClearPixels(SXCCD_EXP_FLAGS_NOWIPE_FRAME,GUIDE_CCD);
                    //  now latch the exposure
                    //rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_EVEN | SXCCD_EXP_FLAGS_NOWIPE_FRAME_FRAME,GUIDE_CCD,GSubX,GSubY,GSubW,GSubH,1,1);
                    rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_EVEN | SXCCD_EXP_FLAGS_NOCLEAR_FRAME,GUIDE_CCD,GSubX,GSubY,GSubW,GSubH,1,1);
                    //rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_BOTH ,GUIDE_CCD,GSubX,GSubY,GSubW,GSubH,1,1);
                    DidGuideLatch=1;
                    IDLog("Guide Even Pixels latched\n");

                } else
                {
                    SetTimer(100);
                }
            } else
            {
                SetTimer(250);
            }
        }
    }

    if(DidLatch==1)
    {
        //  Pixels have been latched
        //  now download them
        int rc;
        rc=ReadCameraFrame(IMAGE_CCD,RawFrame);
        IDLog("Read camera frame with rc=%d\n", rc);
        //rc=ProcessRawData(RawFrame, RawData);
        //IDLog("processed raw data with rc=%d\n", rc);
        DidLatch=0;
        InExposure=false;

/*
        if (StreamSP->s == IPS_BUSY)
        {
            sendPreview();
            StartExposure(0.5);
        }
        else
*/
            ExposureComplete();

        //  if we get here, we quite likely ignored a guider hit
        if(InGuideExposure) SetTimer(1);    //  just make it all run again

    }
    if(DidGuideLatch==1)
    {
        int rc;
        rc=ReadCameraFrame(GUIDE_CCD,RawGuiderFrame);
        DidGuideLatch=0;
        InGuideExposure=false;
        //  send half a frame
        GuideExposureComplete();

        //rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_ODD | SXCCD_EXP_FLAGS_NOWIPE_FRAME,GUIDE_CCD,GSubX,GSubY,GSubW,GSubH,1,1);
        //rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_ODD | SXCCD_EXP_FLAGS_NOWIPE_FRAME,GUIDE_CCD,GSubX,GSubY,GSubW,GSubH,1,1);
        //rc=ReadCameraFrame(GUIDE_CCD,RawGuiderFrame);
        //GuideExposureComplete();
        //ClearPixels(SXCCD_EXP_FLAGS_FIELD_BOTH,GUIDE_CCD);

    }
}

int SxCam::ReadCameraFrame(int index, char *buf)
{
    int rc;
    int numbytes, xwidth=0, yheight=0;
    //static int expCount=0;

    double timesince;
    struct timeval start;
    struct timeval end;

    gettimeofday(&start,NULL);

    if(index==IMAGE_CCD)
    {
            numbytes=SubW*SubH/BinX/BinY;
            xwidth = SubW;
            yheight= SubH;

        if(CamBits==16)
        {
             numbytes=numbytes*2;
             //xwidth  *= 2;
             //yheight *= 2;
        }

        IDLog("SubW: %d - SubH: %d - BinX: %d - BinY: %d CamBits %d\n",SubW, SubH, BinX, BinY,CamBits);

        if (Interlaced)
        {
            numbytes /= 2;

            char *evenBuf, *oddBuf;
            evenBuf = new char[numbytes];
            oddBuf = new char[numbytes];

            // Let's read EVEN fields now
            IDLog("EVEN FIELD: Read Starting for %d\n",numbytes);

            rc=ReadPixels(evenBuf,numbytes);

            IDLog("EVEN FIELD: Read %d\n",rc);

            rc=LatchPixels(SXCCD_EXP_FLAGS_FIELD_ODD,IMAGE_CCD,SubX,SubY,SubW,SubH,BinX,BinY);

            IDLog("bpp: %d - xwidth: %d - ODD FIELD: Read Starting for %d\n", (CamBits==16) ? 2 : 1, xwidth, numbytes);

            rc=ReadPixels(oddBuf,numbytes);

            IDLog("ODD FIELD: Read %d\n",rc);

            for (int i=0; i < SubH ; i+=2)
            {
                memcpy(buf + i * xwidth, evenBuf, xwidth);
                memcpy(buf + ((i+1) * xwidth), oddBuf, xwidth);
            }

              delete (evenBuf);
              delete (oddBuf);

        }
        else
        {

                IDLog("non interlaced Read Starting for %d\n",numbytes);
                rc=ReadPixels(buf,numbytes);
        }
    } else
    {
        numbytes=GSubW*GSubH;
        //numbytes=numbytes*2;
        IDLog("Download Starting for %d\n",numbytes);
        rc=ReadPixels(buf,numbytes);

    }

    gettimeofday(&end,NULL);

    timesince=(double)(end.tv_sec * 1000.0 + end.tv_usec/1000) - (double)(start.tv_sec * 1000.0 + start.tv_usec/1000);
    timesince=timesince/1000;

    IDLog("Download returns %d in %4.2f seconds\n",rc,timesince);
    return rc;
}


int SxCam::SetCamTimer(int msec)
{
    char setup_data[12];
    int rc;

    setup_data[USB_REQ_TYPE    ] = USB_REQ_VENDOR | USB_REQ_DATAOUT;
    setup_data[USB_REQ         ] = SXUSB_SET_TIMER;
    setup_data[USB_REQ_VALUE_L ] = 0;
    setup_data[USB_REQ_VALUE_H ] = 0;
    setup_data[USB_REQ_INDEX_L ] = 0;
    setup_data[USB_REQ_INDEX_H ] = 0;
    setup_data[USB_REQ_LENGTH_L] = 4;
    setup_data[USB_REQ_LENGTH_H] = 0;
    setup_data[USB_REQ_DATA + 0] = msec;
    setup_data[USB_REQ_DATA + 1] = msec >> 8;
    setup_data[USB_REQ_DATA + 2] = msec >> 16;
    setup_data[USB_REQ_DATA + 3] = msec >> 24;

    rc=WriteBulk(setup_data,12,1000);
    return 0;
}

int SxCam::GetCamTimer()
{
    char setup_data[8];
    unsigned int timer;
    int rc;

    setup_data[USB_REQ_TYPE    ] = USB_REQ_VENDOR | USB_REQ_DATAIN;
    setup_data[USB_REQ         ] = SXUSB_GET_TIMER;
    setup_data[USB_REQ_VALUE_L ] = 0;
    setup_data[USB_REQ_VALUE_H ] = 0;
    setup_data[USB_REQ_INDEX_L ] = 0;
    setup_data[USB_REQ_INDEX_H ] = 0;
    setup_data[USB_REQ_LENGTH_L] = 4;
    setup_data[USB_REQ_LENGTH_H] = 0;
    rc=WriteBulk(setup_data,8,1000);
    timer=0;
    rc=ReadBulk((char *)&timer,4,1000);
    return timer;
}

int SxCam::SetParams(int xres,int yres,int Bits,float pixwidth,float pixheight)
{
    IDLog("SxCam::Setparams %d %d\n",xres,yres);
    SetCCDParams(xres,yres,Bits,pixwidth,pixheight);
    CamBits=Bits;
    if (RawFrame != NULL)
        delete RawFrame;

        RawFrameSize=XRes*YRes;                 //  this is pixel count
        if(bits_per_pixel==16) RawFrameSize=RawFrameSize*2;            //  Each pixel is 2 bytes
        RawFrameSize+=512;                      //  leave a little extra at the end
        RawFrame=new char[RawFrameSize];



}

int SxCam::SetGuideParams(int gXRes,int gYRes,int gBits,float gpixwidth,float gpixheight)
{
    IDLog("SxCam::SetGuideparams %d %d\n",xres,yres);

    SetGuidHeadParams(gXRes,gYRes,gBits,gpixwidth,gpixheight);

                RawGuideSize=GXRes*GYRes;
                if(gparms.bits_per_pixel ==16) RawGuideSize=RawGuideSize*2;
                RawGuiderFrame=new char[RawGuideSize];


}

int SxCam::SetInterlaced(bool i)
{
    Interlaced=i;
    return 0;
}