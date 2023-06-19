#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "sd.h"
#include "mp3.h"
#include "taskMonitor.h"

#define EXAMPLE_STD_BCLK_IO1        GPIO_NUM_1      // I2S bit clock io number   I2S_BCLK
#define EXAMPLE_STD_WS_IO1          GPIO_NUM_3      // I2S word select io number    I2S_LRC
#define EXAMPLE_STD_DOUT_IO1        GPIO_NUM_2     // I2S data out io number    I2S_DOUT
#define EXAMPLE_STD_DIN_IO1         GPIO_NUM_NC    // I2S data in io number

#define SAMPLE_RATE 44100

static i2s_chan_handle_t tx_chan;        // I2S tx channel handler



#define OUTPUT_BUFFER_SIZE 2304 //一帧有1152个点

static unsigned short MP3_Data[OUTPUT_BUFFER_SIZE];

unsigned short *get_framebuf() {
    return MP3_Data;
}

void open_sound() {
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void close_sound() {
    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));

}


void submit_framebuf() {
    for (int i = 0; i < OUTPUT_BUFFER_SIZE; ++i) {
        MP3_Data[i] = ((uint16_t)(((int64_t) MP3_Data[i])+UINT16_MAX/2))>>6;
    }
    i2s_channel_write(tx_chan, MP3_Data, OUTPUT_BUFFER_SIZE * 2, NULL, 1000);
}



static void i2s_example_init_std_simplex(void) {
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));


    i2s_std_config_t tx_std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),

            .gpio_cfg = {
                    .mclk = I2S_GPIO_UNUSED,    // some codecs may require mclk signal, this example doesn't need it
                    .bclk = EXAMPLE_STD_BCLK_IO1,
                    .ws   = EXAMPLE_STD_WS_IO1,
                    .dout = EXAMPLE_STD_DOUT_IO1,
                    .din  = EXAMPLE_STD_DIN_IO1,
                    .invert_flags = {
                            .mclk_inv = false,
                            .bclk_inv = false,
                            .ws_inv   = false,
                    },
            },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
}


#define READBUF_SIZE 1024 * 12		 //4000//4096//4000       // Value must min be 2xMAINBUF_SIZE = 2x1940 = 3880bytes
unsigned char readBuf[READBUF_SIZE]; // Read buffer where data from SD card is read to

int MpegAudioDecoder(FILE *InputFp)
{
    // Content is the output from MP3GetLastFrameInfo,
    // we only read this once, and conclude it will be the same in all frames
    // Maybe this needs to be changed, for different requirements.
    MP3FrameInfo mp3FrameInfo;
    int bytesLeft;			// Saves how many bytes left in readbuf
    unsigned char *readPtr; // Pointer to the next new data
    int offset;				// Used to save the offset to the next frame

    //检查MP3文件格式
    MP3CTRL mp3ctrl; //mp3控制结构体

    uint8_t rst = mp3_get_info(InputFp, readBuf, READBUF_SIZE, &mp3ctrl);
    if (rst)
        return 1;
    printf("mp3_get_info:\n");

    /* Decode stdin to stdout. */
    printf("title:%s\n", mp3ctrl.title);
    printf("artist:%s\n", mp3ctrl.artist);
    printf("bitrate:%lubps\n", mp3ctrl.bitrate);
    printf("samplerate:%lu\n", mp3ctrl.samplerate);
    printf("totalsec:%lu\n", mp3ctrl.totsec);
    printf("mp3ctrl.datastart:%lx\n", mp3ctrl.datastart);

    fseek(InputFp, mp3ctrl.datastart, SEEK_SET); //跳过文件头中tag信息

    /* Initilizes the MP3 Library */
    // hMP3Decoder: Content is the pointers to all buffers and information for the MP3 Library

    printf("MP3InitDecoder:\n");

    HMP3Decoder hMP3Decoder = MP3InitDecoder();
    if (hMP3Decoder == 0)
    {
        // 这意味着存储器分配失败。这通常在堆存储空间不足时发生。
        // 请使用其他堆存储空间重新编译代码。
        printf("MP3 Decoder init failed!\n");
        return 1;
    }

    printf("MP3InitDecoder:end\n");

    open_sound();

    while (1)
    {

        bytesLeft = 0;
        readPtr = readBuf;
        printf("#");
        int fres = fread(readBuf, 1, READBUF_SIZE, InputFp);
        if (fres <= 0)
        {
            printf("read file failed!\n");
            goto exit;
        }

        bytesLeft += fres;
        //printf("1 readBuf = %X,readPtr = %X, bytesLeft %d\n", readBuf, readPtr, bytesLeft);

        while (1)
        {
            printf("#");
            /* find start of next MP3 frame - assume EOF if no sync found */
            int offset = MP3FindSyncWord(readPtr, bytesLeft);
            if (offset < 0)
            {
                break;
            }

            readPtr += offset;	 //data start point
            bytesLeft -= offset; //in buffer
            int errs = MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, (short *)get_framebuf(), 0);

            if (errs != ERR_MP3_NONE)
            {
                printf("err code %d ,readBuf = %s,readPtr = %s, bytesLeft %d\n", errs, readBuf, readPtr, bytesLeft);
                switch (errs)
                {
                    case ERR_MP3_INVALID_FRAMEHEADER:
                        printf("INVALID_FRAMEHEADER\n");
                        //bytesLeft = 0;
                        //readPtr = readBuf;
                        //continue;
                        goto exit;
                        break;
                    case ERR_MP3_INDATA_UNDERFLOW:
                        printf("INDATA_UNDERFLOW\n");
                        goto exit;
                        break;
                    case ERR_MP3_MAINDATA_UNDERFLOW:
                        printf("MAINDATA_UNDERFLOW\n");
                        //bytesLeft = READBUF_SIZE;
                        //readPtr = readBuf;
                        //continue;
                        goto exit;
                        break;
                    case ERR_MP3_FREE_BITRATE_SYNC:
                        printf("FREE_BITRATE_SYNC\n");
                        goto exit;
                        break;
                    default:
                        printf("ERR\n");
                        goto exit;
                        break;
                }
            }
            MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);

            if (bytesLeft < MAINBUF_SIZE * 2)
            {
                memmove(readBuf, readPtr, bytesLeft);
                fres = fread(readBuf + bytesLeft, 1, READBUF_SIZE - bytesLeft, InputFp);
                if ((fres <= 0))
                {
                    printf("fread exit\n");
                    goto exit;
                }

                if (fres < READBUF_SIZE - bytesLeft)
                    memset(readBuf + bytesLeft + fres, 0, READBUF_SIZE - bytesLeft - fres);
                bytesLeft = READBUF_SIZE;
                readPtr = readBuf;
            }

            submit_framebuf();
        }
    }

    exit:
    close_sound();
    MP3FreeDecoder(hMP3Decoder);
    return 0;
}

void app_main(void) {
    startTaskMonitor(10000);
    i2s_example_init_std_simplex();

    init_sd();
    char *file_name = "/sdcard/oo.mp3";
    calc_file_size(file_name);

    FILE *fp = fopen(file_name, "rb");

    printf("begin to decode %s\n", file_name);
    int Status = MpegAudioDecoder(fp);
    printf("end decode %s\n", file_name);
    if (Status)
    {
        printf("an error occurred during decoding %s.\n", file_name);
    }
    fclose(fp);




    /* Step 4: Create writing and reading task */
//    xTaskCreate(i2s_example_write_task, "i2s_example_write_task", 4096, NULL, 5, NULL);
}
