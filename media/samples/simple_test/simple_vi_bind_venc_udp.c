#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_venc.h"

/* Sensor input */
#define VI_WIDTH          2304
#define VI_HEIGHT         1296
#define VI_BUF_COUNT      3
#define APP_VERSION       "v1.3.1-320x240-fpv-noslice"

/* Encoder defaults */
#define DEFAULT_OUT_W     320
#define DEFAULT_OUT_H     240
#define SRC_FPS           30
#define DST_FPS           24
#define BITRATE_BPS       (320 * 1024)   /* 320kbps */
#define GOP_SIZE          30             /* 更短 GOP，配合 Intra Refresh */

/* RTP */
#define RTP_MAX_PAYLOAD   1400
#define RTP_PT            96
#define RTP_SSRC          0x12345678
#define RTP_TS_INC        (90000 / DST_FPS)

/* 彻底禁止突发高于编码码率 */
#define MAX_UDP_TX_BPS    (BITRATE_BPS * 1)

typedef struct {
    uint8_t  v_p_x_cc;
    uint8_t  m_pt;
    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
} __attribute__((packed)) rtp_hdr_t;

typedef struct {
    const uint8_t *ptr;
    uint32_t       len;
    bool           marker;
} nal_item_t;

static volatile bool g_quit = false;

static void sig_handler(int sig) {
    (void)sig;
    g_quit = true;
}

/* ================= 时间工具 ================= */

static inline uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* ================= RTP NAL sender ================= */

static void rtp_send_nal(int fd, struct sockaddr_in *dst,
                         const uint8_t *nal, uint32_t len,
                         uint16_t *seq, uint32_t ts, bool marker)
{
    uint8_t pkt[sizeof(rtp_hdr_t) + RTP_MAX_PAYLOAD];
    rtp_hdr_t *rh = (rtp_hdr_t *)pkt;

    if (len <= RTP_MAX_PAYLOAD) {
        rh->v_p_x_cc  = 0x80;
        rh->m_pt      = (marker ? 0x80 : 0x00) | RTP_PT;
        rh->seq       = htons((*seq)++);
        rh->timestamp = htonl(ts);
        rh->ssrc      = htonl(RTP_SSRC);

        memcpy(pkt + sizeof(rtp_hdr_t), nal, len);
        size_t pkt_len = sizeof(rtp_hdr_t) + len;

        sendto(fd, pkt, pkt_len, 0, (struct sockaddr *)dst, sizeof(*dst));
        return;
    }

    /* FU-A fragmentation */
    uint8_t nri    = nal[0] & 0x60;
    uint8_t type   = nal[0] & 0x1F;
    uint8_t fu_ind = nri | 28;
    uint32_t off   = 1;

    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > RTP_MAX_PAYLOAD - 2)
            chunk = RTP_MAX_PAYLOAD - 2;

        bool first = (off == 1);
        bool last  = (off + chunk >= len);

        rh->v_p_x_cc  = 0x80;
        rh->m_pt      = ((last && marker) ? 0x80 : 0x00) | RTP_PT;
        rh->seq       = htons((*seq)++);
        rh->timestamp = htonl(ts);
        rh->ssrc      = htonl(RTP_SSRC);

        uint8_t *pl = pkt + sizeof(rtp_hdr_t);
        pl[0] = fu_ind;
        pl[1] = type | (first ? 0x80 : 0x00) | (last ? 0x40 : 0x00);
        memcpy(pl + 2, nal + off, chunk);

        size_t pkt_len = sizeof(rtp_hdr_t) + 2 + chunk;
        sendto(fd, pkt, pkt_len, 0, (struct sockaddr *)dst, sizeof(*dst));

        off += chunk;
    }
}

/* ================= RTP frame sender（时间片 pacing） ================= */

static void rtp_send_frame_paced(int fd, struct sockaddr_in *dst,
                                 uint8_t *data, uint32_t len,
                                 uint16_t *seq, uint32_t ts)
{
    nal_item_t nals[64];
    uint32_t   nal_count = 0;

    /* 解析 NAL 边界（假设起始码不会跨 pack，这里只在当前 buffer 内找） */
    uint32_t off = 0;
    while (off + 4 < len && nal_count < 64) {
        if (data[off] == 0x00 && data[off+1] == 0x00 &&
            data[off+2] == 0x00 && data[off+3] == 0x01)
        {
            uint32_t nal_start = off + 4;
            uint32_t nal_end   = nal_start;

            while (nal_end + 4 < len) {
                if (data[nal_end] == 0x00 && data[nal_end+1] == 0x00 &&
                    data[nal_end+2] == 0x00 && data[nal_end+3] == 0x01)
                    break;
                nal_end++;
            }

            nals[nal_count].ptr    = data + nal_start;
            nals[nal_count].len    = nal_end - nal_start;
            nals[nal_count].marker = false;
            nal_count++;

            off = nal_end;
        } else {
            off++;
        }
    }
    if (nal_count == 0)
        return;
    nals[nal_count - 1].marker = true;

    /* 一帧时间预算（例如 24fps -> ~41666us） */
    const uint64_t frame_budget_us = 1000000ULL / DST_FPS;
    uint64_t start_us = now_us();

    /* 按 NAL 序号线性分配时间片，边发边 sleep，避免瞬时洪峰 */
    for (uint32_t i = 0; i < nal_count; i++) {
        uint64_t now = now_us();
        uint64_t elapsed = now - start_us;

        uint64_t target_us = frame_budget_us * (i + 1) / nal_count;
        if (elapsed < target_us) {
            usleep((useconds_t)(target_us - elapsed));
        }

        rtp_send_nal(fd, dst, nals[i].ptr, nals[i].len, seq, ts, nals[i].marker);
    }

    /* 如果整帧发完仍然比预算快一点，再补一个小 sleep，避免下一帧太早 */
    uint64_t total_elapsed = now_us() - start_us;
    if (total_elapsed < frame_budget_us) {
        usleep((useconds_t)(frame_budget_us - total_elapsed));
    }
}

/* ================= VENC thread ================= */

static void *venc_thread(void *arg)
{
    (void)arg;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return NULL; }

    int sndbuf = 64 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(5600);
    inet_aton("127.0.0.1", &dst.sin_addr);

    VENC_STREAM_S frame;
    memset(&frame, 0, sizeof(frame));
    VENC_PACK_S packs[16];
    frame.pstPack = packs;
    frame.u32PackCount = 16;

    uint16_t seq = 0;
    uint32_t ts  = 0;

    uint32_t frm_cnt = 0, sec_bytes = 0, sec_max = 0;
    uint64_t last_stat_us = now_us();

    while (!g_quit) {
        if (RK_MPI_VENC_GetStream(0, &frame, 100) != RK_SUCCESS)
            continue;

        /* 新一帧：时间戳前进一次 */
        ts += RTP_TS_INC;

        uint32_t frame_total = 0;

        /* 不再做整帧 memcpy，直接对每个 pack 的 buffer 做 NAL 解析 + RTP 发送 */
        for (uint32_t i = 0; i < frame.u32PackCount; i++) {
            if (frame.pstPack[i].u32Len == 0)
                continue;

            uint8_t *ptr = (uint8_t *)RK_MPI_MB_Handle2VirAddr(frame.pstPack[i].pMbBlk);
            if (!ptr) continue;

            uint32_t len = frame.pstPack[i].u32Len;
            frame_total += len;

            /* 这里仍然按“整帧时间预算”来 pacing，但在 pack 内部做 NAL 解析 */
            rtp_send_frame_paced(fd, &dst, ptr, len, &seq, ts);
        }

        printf("."); fflush(stdout);

        frm_cnt++;
        sec_bytes += frame_total;
        if (frame_total > sec_max) sec_max = frame_total;

        uint64_t now = now_us();
        if (now - last_stat_us >= 1000000ULL) {
            printf("\n[VENC] %u frames, %u bytes/s, max_frame=%u\n",
                   frm_cnt, sec_bytes, sec_max);
            frm_cnt = 0;
            sec_bytes = 0;
            sec_max = 0;
            last_stat_us = now;
        }

        RK_MPI_VENC_ReleaseStream(0, &frame);
    }

    close(fd);
    return NULL;
}

/* ================= Pipeline init ================= */

static int vi_dev_init(void)
{
    VI_DEV_ATTR_S dev_attr;
    memset(&dev_attr, 0, sizeof(dev_attr));

    RK_S32 ret = RK_MPI_VI_GetDevAttr(0, &dev_attr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(0, &dev_attr);
        if (ret != RK_SUCCESS) return ret;
    }

    if (RK_MPI_VI_GetDevIsEnable(0) != RK_SUCCESS) {
        ret = RK_MPI_VI_EnableDev(0);
        if (ret != RK_SUCCESS) return ret;

        VI_DEV_BIND_PIPE_S bind;
        memset(&bind, 0, sizeof(bind));
        bind.u32Num = 1;
        bind.PipeId[0] = 0;
        ret = RK_MPI_VI_SetDevBindPipe(0, &bind);
        if (ret != RK_SUCCESS) return ret;
    }

    return RK_SUCCESS;
}

static int vi_chn_init(void)
{
    VI_CHN_ATTR_S attr;
    memset(&attr, 0, sizeof(attr));

    attr.stIspOpt.u32BufCount         = VI_BUF_COUNT;
    attr.stIspOpt.enMemoryType        = VI_V4L2_MEMORY_TYPE_DMABUF;
    attr.stIspOpt.stMaxSize.u32Width  = VI_WIDTH;
    attr.stIspOpt.stMaxSize.u32Height = VI_HEIGHT;
    attr.stSize.u32Width              = VI_WIDTH;
    attr.stSize.u32Height             = VI_HEIGHT;
    attr.enPixelFormat                = RK_FMT_YUV420SP;
    attr.stFrameRate.s32SrcFrameRate  = SRC_FPS;
    attr.stFrameRate.s32DstFrameRate  = DST_FPS;

    RK_S32 ret = RK_MPI_VI_SetChnAttr(0, 0, &attr);
    if (ret != RK_SUCCESS) return ret;

    return RK_MPI_VI_EnableChn(0, 0);
}

static int vpss_init(int w, int h)
{
    VPSS_GRP_ATTR_S grp;
    memset(&grp, 0, sizeof(grp));
    grp.u32MaxW       = VI_WIDTH;
    grp.u32MaxH       = VI_HEIGHT;
    grp.enPixelFormat = RK_FMT_YUV420SP;

    RK_S32 ret = RK_MPI_VPSS_CreateGrp(0, &grp);
    if (ret != RK_SUCCESS) return ret;

    ret = RK_MPI_VPSS_StartGrp(0);
    if (ret != RK_SUCCESS) return ret;

    VPSS_CHN_ATTR_S chn;
    memset(&chn, 0, sizeof(chn));
    chn.enChnMode     = VPSS_CHN_MODE_USER;
    chn.enPixelFormat = RK_FMT_YUV420SP;
    chn.u32Width      = w;
    chn.u32Height     = h;

    ret = RK_MPI_VPSS_SetChnAttr(0, 0, &chn);
    if (ret != RK_SUCCESS) return ret;

    return RK_MPI_VPSS_EnableChn(0, 0);
}

static int venc_init(int w, int h)
{
    VENC_CHN_ATTR_S attr;
    memset(&attr, 0, sizeof(attr));

    attr.stVencAttr.enType          = RK_VIDEO_ID_AVC;
    attr.stVencAttr.enPixelFormat   = RK_FMT_YUV420SP;
    attr.stVencAttr.u32Profile      = 100;
    attr.stVencAttr.u32PicWidth     = w;
    attr.stVencAttr.u32PicHeight    = h;
    attr.stVencAttr.u32VirWidth     = w;
    attr.stVencAttr.u32VirHeight    = h;
    attr.stVencAttr.u32BufSize      = w * h * 3 / 2;
    attr.stVencAttr.u32StreamBufCnt = 3;

    attr.stRcAttr.enRcMode                      = VENC_RC_MODE_H264CBR;
    attr.stRcAttr.stH264Cbr.u32BitRate          = BITRATE_BPS;
    attr.stRcAttr.stH264Cbr.u32Gop              = GOP_SIZE;
    attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum  = SRC_FPS;
    attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen  = 1;
    attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = DST_FPS;
    attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;

    RK_S32 ret = RK_MPI_VENC_CreateChn(0, &attr);
    if (ret != RK_SUCCESS) return ret;

    /* QP 调整：宁可糊也不爆 */
    VENC_RC_PARAM_S rc;
    memset(&rc, 0, sizeof(rc));
    RK_MPI_VENC_GetRcParam(0, &rc);
    rc.s32FirstFrameStartQp   = 32;
    rc.stParamH264.u32MinQp   = 30;
    rc.stParamH264.u32MaxQp   = 51;
    rc.stParamH264.u32MinIQp  = 32;
    rc.stParamH264.u32MaxIQp  = 51;
    RK_MPI_VENC_SetRcParam(0, &rc);

    /* Intra Refresh：行刷新，温和持续刷新，避免巨型 I 帧 */
    VENC_INTRA_REFRESH_S stIntraRefresh;
    memset(&stIntraRefresh, 0, sizeof(stIntraRefresh));
    if (RK_MPI_VENC_GetIntraRefresh(0, &stIntraRefresh) == RK_SUCCESS) {
        stIntraRefresh.bRefreshEnable     = RK_TRUE;
        stIntraRefresh.enIntraRefreshMode = INTRA_REFRESH_ROW;
        stIntraRefresh.u32RefreshNum      = (h / 32 > 0) ? (h / 32) : 1;
        RK_MPI_VENC_SetIntraRefresh(0, &stIntraRefresh);
    }

    VENC_RECV_PIC_PARAM_S recv;
    memset(&recv, 0, sizeof(recv));
    recv.s32RecvPicNum = -1;

    return RK_MPI_VENC_StartRecvFrame(0, &recv);
}

/* ================= Main ================= */

int main(int argc, char *argv[])
{
    int out_w = DEFAULT_OUT_W;
    int out_h = DEFAULT_OUT_H;

    int opt;
    while ((opt = getopt(argc, argv, "w:h:")) != -1) {
        switch (opt) {
        case 'w': out_w = atoi(optarg); break;
        case 'h': out_h = atoi(optarg); break;
        }
    }

    printf("==================================================\n");
    printf("   Luckfox FPV Video Streamer - %s\n", APP_VERSION);
    printf("==================================================\n");
    printf("Output: %dx%d @ %dfps, bitrate: %dKbps, GOP: %d\n",
           out_w, out_h, DST_FPS, BITRATE_BPS / 1024, GOP_SIZE);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    if (RK_MPI_SYS_Init()               != RK_SUCCESS) return -1;
    if (vi_dev_init()                    != RK_SUCCESS) return -1;
    if (vi_chn_init()                    != RK_SUCCESS) return -1;
    if (vpss_init(out_w, out_h)          != RK_SUCCESS) return -1;
    if (venc_init(out_w, out_h)          != RK_SUCCESS) return -1;

    MPP_CHN_S vi_ch   = { RK_ID_VI,   0, 0 };
    MPP_CHN_S vpss_ch = { RK_ID_VPSS, 0, 0 };
    MPP_CHN_S venc_ch = { RK_ID_VENC, 0, 0 };

    if (RK_MPI_SYS_Bind(&vi_ch, &vpss_ch)   != RK_SUCCESS) return -1;
    if (RK_MPI_SYS_Bind(&vpss_ch, &venc_ch) != RK_SUCCESS) return -1;

    pthread_t th;
    pthread_create(&th, NULL, venc_thread, NULL);

    while (!g_quit) usleep(100000);

    printf("Exiting cleanup...\n");
    g_quit = true;
    pthread_join(th, NULL);

    RK_MPI_SYS_UnBind(&vpss_ch, &venc_ch);
    RK_MPI_SYS_UnBind(&vi_ch, &vpss_ch);
    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);
    RK_MPI_VPSS_DisableChn(0, 0);
    RK_MPI_VPSS_StopGrp(0);
    RK_MPI_VPSS_DestroyGrp(0);
    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);
    RK_MPI_SYS_Exit();

    return 0;
}
