/* [FW-UPD] 관리서버 UDP 앱 업데이트 부트로더 모듈 — 본체
 * 6.10 FW_CHUNK / 6.11 FW_END 수신 상태 머신 + 6.12 FW_REPORT 보고.
 * EVTIND 없이 운영: CGATT 폴링. 프레임: [0xA3][msgLen][cmd][payload][checksum]
 * (msgLen=cmd+payload 길이, checksum=header~payload 합산 하위 8비트 — app_nbiot.c 동일) */
#include "boot_udp_dl.h"
#include "boot_crc.h"
#include "boot_info.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

static uint8_t  s_serverIp[4];
static uint16_t s_serverPort;

/* ===== UART (LPUART1, 9600, RX IT) ===== */
static UART_HandleTypeDef s_hlpuart;
static volatile uint8_t  s_rxByte;
static volatile char     s_line[256];
static volatile uint16_t s_lineLen;
static volatile uint8_t  s_lineReady;

void BootUdpDlUartIrq(void)
{
    HAL_UART_IRQHandler(&s_hlpuart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h->Instance == LPUART1)
    {
        uint8_t b = s_rxByte;
        if (b == '\n')
        {
            s_lineReady = 1u;   /* 빈 줄 포함 소비(길이 0 라인은 파서에서 무시) */
        }
        else if (b != '\r')
        {
            if (s_lineLen < (sizeof(s_line) - 1u)) { s_line[s_lineLen++] = (char)b; }
        }
        (void)HAL_UART_Receive_IT(&s_hlpuart, (uint8_t *)&s_rxByte, 1u);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h->Instance == LPUART1)
    {
        /* ORE/FE 등: 라인 폐기 후 재수신 재개 */
        s_lineLen = 0u;
        s_lineReady = 0u;
        (void)HAL_UART_Receive_IT(&s_hlpuart, (uint8_t *)&s_rxByte, 1u);
    }
}

static void UartInit(void)
{
    __HAL_RCC_LPUART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /* PA2=TX PA3=RX (기존 프로젝트 핀맵과 동일 가정) */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF6_LPUART1;
    HAL_GPIO_Init(GPIOA, &g);

    s_hlpuart.Instance = LPUART1;
    s_hlpuart.Init.BaudRate = 9600;
    s_hlpuart.Init.WordLength = UART_WORDLENGTH_8B;
    s_hlpuart.Init.StopBits = UART_STOPBITS_1;
    s_hlpuart.Init.Parity = UART_PARITY_NONE;
    s_hlpuart.Init.Mode = UART_MODE_TX_RX;
    s_hlpuart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_hlpuart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    HAL_UART_Init(&s_hlpuart);

    HAL_NVIC_SetPriority(LPUART1_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
    s_lineLen = 0u; s_lineReady = 0u;
    (void)HAL_UART_Receive_IT(&s_hlpuart, (uint8_t *)&s_rxByte, 1u);
}

/* ===== AT 헬퍼 ===== */
static const char *AtWaitPrefix(const char *prefix, uint32_t timeoutMs)
{
    uint32_t t0 = HAL_GetTick();
    uint16_t plen = (uint16_t)strlen(prefix);
    for (;;)
    {
        if (s_lineReady != 0u)
        {
            if ((s_lineLen >= plen) &&
                (strncmp((const char *)s_line, prefix, plen) == 0))
            {
                static char line[256];
                memcpy(line, (const void *)s_line, s_lineLen + 1u);
                s_lineReady = 0u; s_lineLen = 0u;
                return line;
            }
            s_lineReady = 0u; s_lineLen = 0u;
        }
        if ((HAL_GetTick() - t0) > timeoutMs) { return NULL; }
    }
}

static void AtFlush(void)
{
    s_lineLen = 0u; s_lineReady = 0u;
}

static void AtSend(const char *cmd)
{
    HAL_UART_Transmit(&s_hlpuart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000u);
    HAL_UART_Transmit(&s_hlpuart, (uint8_t *)"\r\n", 2u, 100u);
    AtFlush();
}

static int AtOkCmd(const char *cmd, uint32_t timeoutMs)
{
    AtSend(cmd);
    return (AtWaitPrefix("OK", timeoutMs) != NULL) ? 1 : 0;
}

/* ===== UDP 패킷 수신 ===== */
typedef struct
{
    uint8_t data[BOOT_DL_CHUNK_MAX + 8u];
    uint16_t dataLen;
} DlPacket_t;

static uint8_t HexNibble(char c, uint8_t *out)
{
    if ((c >= '0') && (c <= '9')) { *out = (uint8_t)(c - '0'); return 1u; }
    if ((c >= 'A') && (c <= 'F')) { *out = (uint8_t)(c - 'A' + 10); return 1u; }
    if ((c >= 'a') && (c <= 'f')) { *out = (uint8_t)(c - 'a' + 10); return 1u; }
    return 0u;
}

/* 서버 프레임 1개 수신. NSORF 폴링 방식(NSONMI 래치 누락에도 데이터 유실 없음).
 * 반환 1=패킷, 0=타임아웃. */
static int DlWaitPacket(DlPacket_t *pk, uint32_t timeoutMs)
{
    uint32_t t0 = HAL_GetTick();
    for (;;)
    {
        AtSend("AT+NSORF=0");
        const char *l = AtWaitPrefix("+NSORF:", 2000u);
        if (l != NULL)
        {
            /* +NSORF:<sock>,"<ip>",<port>,<len>,"<hex>",<remaining> */
            const char *q = strchr(l, '"');
            if (q != NULL)
            {
                const char *d = strchr(q + 1, '"');
                if (d != NULL)
                {
                    d++;
                    const char *q2 = strchr(d, '"');
                    if (q2 != NULL)
                    {
                        uint16_t n = 0u;
                        while (((d + (n * 2u)) < q2) && (n < sizeof(pk->data)))
                        {
                            uint8_t hi, lo;
                            if ((HexNibble(d[n * 2u], &hi) == 0u) ||
                                (HexNibble(d[(n * 2u) + 1u], &lo) == 0u))
                            {
                                n = 0u;
                                break;
                            }
                            pk->data[n++] = (uint8_t)((hi << 4) | lo);
                        }
                        if (n > 0u)
                        {
                            pk->dataLen = n;
                            return 1;
                        }
                    }
                }
            }
            /* 파싱 실패 라인: 폐기 후 재폴링 */
        }
        if ((HAL_GetTick() - t0) > timeoutMs) { return 0; }
    }
}

/* 프레임 검증: checksum 통과 시 cmd 반환, 불량 시 0 */
static uint8_t DlFrameCmd(const uint8_t *f, uint16_t len)
{
    if ((len < 5u) || (f[0] != 0xA3u)) { return 0u; }
    uint16_t msgLen = f[1];
    if ((uint16_t)(msgLen + 3u) != len) { return 0u; }
    uint8_t sum = 0u;
    for (uint16_t i = 0u; i < (len - 1u); i++) { sum += f[i]; }
    if (sum != f[len - 1u]) { return 0u; }
    return f[2];
}

/* ===== UDP 프레임 송신 ===== */
static uint16_t DlBuildFrame(uint8_t cmd, const uint8_t *payload, uint16_t plen, uint8_t *out)
{
    uint16_t i = 0u;
    uint8_t sum = 0u;
    out[i++] = 0xA3u; sum += 0xA3u;
    out[i++] = (uint8_t)(1u + plen); sum += (uint8_t)(1u + plen);
    out[i++] = cmd; sum += cmd;
    for (uint16_t j = 0u; j < plen; j++)
    {
        out[i++] = payload[j]; sum += payload[j];
    }
    out[i++] = sum;
    return i;
}

static int DlSendUdp(uint8_t cmd, const uint8_t *payload, uint16_t plen)
{
    uint8_t frame[280];
    uint16_t flen = DlBuildFrame(cmd, payload, plen, frame);
    char hex[580];
    for (uint16_t i = 0u; i < flen; i++) { (void)sprintf(&hex[i * 2u], "%02X", frame[i]); }
    hex[flen * 2u] = 0u;

    char tx[80 + sizeof(hex)];
    (void)sprintf(tx, "AT+NSOST=0,%u.%u.%u.%u,%u,%u,%s",
                  s_serverIp[0], s_serverIp[1], s_serverIp[2], s_serverIp[3],
                  s_serverPort, flen, hex);
    AtSend(tx);
    /* +NSOST: / +NSOSTR: 응답 대기(app_nbiot.c 실장비 파서 접두와 호환) */
    return (AtWaitPrefix("+NSOST", 3000u) != NULL) ? 1 : 0;
}

/* 6.12 FW_REPORT: [result(1)][failCount(1)][recvOffset(4)] */
static void DlSendReport(uint8_t result, const BootDlMeta_t *meta)
{
    uint8_t p[6];
    p[0] = result;
    p[1] = meta->failCount;
    p[2] = (uint8_t)(meta->recvOffset >> 24);
    p[3] = (uint8_t)(meta->recvOffset >> 16);
    p[4] = (uint8_t)(meta->recvOffset >> 8);
    p[5] = (uint8_t)(meta->recvOffset);
    (void)DlSendUdp(BOOT_DL_CMD_FW_REPORT, p, 6u);
}

/* ===== attach / socket ===== */
static int DlAttach(void)
{
    uint32_t t0 = HAL_GetTick();
    (void)AtOkCmd("AT", 1000u);
    (void)AtOkCmd("AT+CMEE=1", 1000u);
    (void)AtOkCmd("AT+CFUN=1", 2000u);
    while ((HAL_GetTick() - t0) < BOOT_DL_ATTACH_TIMEOUT_MS)
    {
        AtSend("AT+CGATT?");
        const char *l = AtWaitPrefix("+CGATT:", 2000u);
        if ((l != NULL) && (l[7] == '1')) { return 1; }
    }
    return 0;
}

static int DlUdpOpen(void)
{
    char tx[48];
    (void)sprintf(tx, "AT+NSOCR=DGRAM,17,%u,1,AF_INET", BOOT_DL_UDP_PORT);
    AtSend(tx);
    /* BC95-GV 응답: 소켓 id 단독 라인(예: "0") */
    const char *l = AtWaitPrefix("0", 3000u);
    return ((l != NULL) && (l[0] == '0')) ? 1 : 0;
}

/* ===== META ===== */
/* 구조체 무결성 CRC: XOR 기반(플래시 워드 합). BootInfo/DlMeta 자체 보호용으로 충분. */
static uint32_t MetaCrc(const BootDlMeta_t *m)
{
    uint32_t c = 0u;
    const uint32_t *w = (const uint32_t *)m;
    for (uint32_t i = 0u; i < (sizeof(*m) / 4u); i++)
    {
        if (i == (offsetof(BootDlMeta_t, crc32) / 4u)) { continue; }
        c ^= w[i];
    }
    return c;
}

static void DlMetaSave(BootDlMeta_t *m)
{
    m->crc32 = MetaCrc(m);
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err;
    HAL_FLASH_Unlock();
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.PageAddress = BOOT_DL_META_PAGE2;
    e.NbPages = 1u;
    if (HAL_FLASHEx_Erase(&e, &err) == HAL_OK)
    {
        const uint32_t *w = (const uint32_t *)m;
        for (uint32_t i = 0u; i < (sizeof(*m) / 4u); i++)
        {
            (void)HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_DL_META_PAGE2 + (i * 4u), w[i]);
        }
    }
    (void)HAL_FLASH_Lock();
}

static void DlMetaLoad(BootDlMeta_t *m)
{
    memcpy(m, (const void *)BOOT_DL_META_PAGE2, sizeof(*m));
    if ((m->magic != BOOT_DL_MAGIC) || (m->crc32 != MetaCrc(m)))
    {
        memset(m, 0, sizeof(*m));
    }
}

/* ===== 플래시 기록 ===== */
static HAL_StatusTypeDef DlEnsureErase(uint32_t addr, uint32_t len)
{
    HAL_StatusTypeDef st = HAL_OK;
    for (uint32_t p = (addr & ~(128u - 1u)); p < (addr + len); p += 128u)
    {
        /* STM32L0 페이지=128B. 내용이 모두 0xFF면 지움 생략(재개·중복 청크 이득) */
        const uint32_t *w = (const uint32_t *)p;
        uint8_t blank = 1u;
        for (uint32_t i = 0u; i < (128u / 4u); i++)
        {
            if (w[i] != 0xFFFFFFFFu) { blank = 0u; break; }
        }
        if (blank == 0u)
        {
            FLASH_EraseInitTypeDef e = {0};
            uint32_t err;
            e.TypeErase = FLASH_TYPEERASE_PAGES;
            e.PageAddress = p;
            e.NbPages = 1u;
            st = HAL_FLASHEx_Erase(&e, &err);
            if (st != HAL_OK) { break; }
        }
    }
    return st;
}

static HAL_StatusTypeDef DlProgramChunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef st;
    uint32_t addr = BOOT_DL_APP_BASE + offset;
    HAL_FLASH_Unlock();
    st = DlEnsureErase(addr, len);
    if (st == HAL_OK)
    {
        for (uint32_t i = 0u; i < len; i += 4u)
        {
            uint32_t w;
            memcpy(&w, &data[i], 4u);   /* len은 4의 배수(서버 패딩) */
            st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, w);
            if (st != HAL_OK) { break; }
        }
    }
    (void)HAL_FLASH_Lock();
    return st;
}

/* 6.11 검증: 다운로드된 슬롯 헤더(magic@0xC0, size@0xC4, crc@0xC8) + 전체 CRC */
static uint8_t DlVerifyImage(const BootDlMeta_t *meta)
{
    const BootImageHeader_t *h = (const BootImageHeader_t *)(BOOT_DL_APP_BASE + BOOT_IMAGE_HEADER_OFFSET);
    if (h->magicNumber != BOOT_MAGIC_NUMBER) { return 0u; }
    if ((h->firmwareSize < (BOOT_IMAGE_HEADER_OFFSET + sizeof(BootImageHeader_t))) ||
        (h->firmwareSize > BOOT_DL_APP_SIZE))
    {
        return 0u;
    }
    if (h->firmwareSize != meta->imageSize) { return 0u; }
    {
        uint32_t crc = BootCRC_CalculateWithPatch(BOOT_DL_APP_BASE, h->firmwareSize,
                       BOOT_IMAGE_HEADER_OFFSET + offsetof(BootImageHeader_t, firmwareCRC));
        return (crc == h->firmwareCRC) ? 1u : 0u;
    }
}

/* ===== 진입점 ===== */
void BootUdpDlRun(void)
{
    BootDlMeta_t meta;
    DlMetaLoad(&meta);
    if ((meta.active != 1u) || (meta.imageSize == 0u))
    {
        return;  /* 요청 없음: boot_main이 정상 점프 */
    }

    s_serverIp[0] = meta.serverIp[0]; s_serverIp[1] = meta.serverIp[1];
    s_serverIp[2] = meta.serverIp[2]; s_serverIp[3] = meta.serverIp[3];
    s_serverPort = meta.serverPort;

    UartInit();

    for (uint8_t attempt = 0u; attempt < BOOT_DL_NET_ATTEMPTS; attempt++)
    {
        if (DlAttach() == 0) { continue; }
        if (DlUdpOpen() == 0) { continue; }

        /* READY: 현재 recvOffset을 서버에 알려 재개 지점 지정 */
        DlSendReport(BOOT_DL_RPT_READY, &meta);

        uint32_t lastSave = meta.recvOffset;
        for (;;)
        {
            DlPacket_t pk;
            if (DlWaitPacket(&pk, BOOT_DL_IDLE_TIMEOUT_MS) == 0)
            {
                break;   /* 무응답: 이번 부팅 실패 누적으로 마무리 */
            }

            uint8_t cmd = DlFrameCmd(pk.data, pk.dataLen);
            if ((cmd != BOOT_DL_CMD_FW_CHUNK) && (cmd != BOOT_DL_CMD_FW_END))
            {
                continue;   /* 비관련 프레임 무시 */
            }

            if (cmd == BOOT_DL_CMD_FW_CHUNK)
            {
                if (pk.dataLen < 10u) { continue; }  /* 최소: cmd(1)+offset(4)+len(2)+payload(1) */
                const uint8_t *p = &pk.data[3];
                uint32_t offset = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                                  ((uint32_t)p[2] << 8) | (uint32_t)p[3];
                uint16_t len = (uint16_t)(((uint16_t)p[4] << 8) | (uint16_t)p[5]);
                if ((len == 0u) || (len > BOOT_DL_CHUNK_MAX) ||
                    (offset > BOOT_DL_APP_SIZE) || ((offset + len) > BOOT_DL_APP_SIZE))
                {
                    continue;
                }

                uint8_t status = BOOT_DL_ACK_OK;
                if (offset == meta.recvOffset)
                {
                    /* 4바이트 정렬 보정(서버는 4의 배수로 분할·패딩) */
                    uint16_t plen = (uint16_t)(len & ~3u);
                    if (plen == 0u) { continue; }
                    if (DlProgramChunk(offset, &p[6], plen) == HAL_OK)
                    {
                        meta.recvOffset = offset + plen;
                        /* 4KB 단위 저장(전원차단 재개점) */
                        if ((meta.recvOffset - lastSave) >= 4096u)
                        {
                            DlMetaSave(&meta);
                            lastSave = meta.recvOffset;
                        }
                    }
                    else
                    {
                        status = BOOT_DL_ACK_GAP;   /* 프로그램 실패: 재전송 요청 */
                    }
                }
                else
                {
                    status = BOOT_DL_ACK_GAP;   /* 순서 어긋남: 서버가 recvOffset으로 재동기화 */
                }

                {
                    uint8_t ack[7];
                    ack[0] = (uint8_t)(offset >> 24); ack[1] = (uint8_t)(offset >> 16);
                    ack[2] = (uint8_t)(offset >> 8);  ack[3] = (uint8_t)(offset);
                    ack[4] = (uint8_t)(len >> 8);     ack[5] = (uint8_t)(len);
                    ack[6] = status;
                    (void)DlSendUdp(BOOT_DL_CMD_FW_CHUNK, ack, 7u);
                }
            }
            else  /* FW_END */
            {
                if (pk.dataLen < 7u) { continue; }
                const uint8_t *p = &pk.data[3];
                uint32_t endCrc = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                                  ((uint32_t)p[2] << 8) | (uint32_t)p[3];

                if ((endCrc == meta.imageCrc) && (meta.recvOffset >= meta.imageSize) &&
                    (DlVerifyImage(&meta) != 0u))
                {
                    meta.active = 0u;
                    meta.failCount = 0u;
                    DlMetaSave(&meta);
                    DlSendReport(BOOT_DL_RPT_SUCCESS, &meta);
                    return;   /* boot_main이 새 이미지 검증 후 점프 */
                }

                /* CRC 불일치 */
                meta.failCount++;
                if (meta.failCount >= BOOT_DL_FAIL_LIMIT)
                {
                    DlSendReport(BOOT_DL_RPT_FAIL_LIMIT, &meta);
                    meta.active = 0u;   /* 무한 재시도 차단: 보고 후 idle */
                    DlMetaSave(&meta);
                }
                else
                {
                    DlSendReport(BOOT_DL_RPT_FAIL_CRC, &meta);
                    meta.recvOffset = 0u;   /* 다음 부팅에서 풀재시도 */
                    DlMetaSave(&meta);
                }
                return;
            }
        }
        break;  /* 첫 네트워크 시도에서 무응답이면 재시도 없이 마무리(부팅 단위 재시도) */
    }

    /* 도달: attach/socket 실패 또는 무응답 — 부팅 단위 실패 누적.
     * attach 실패 시에는 보고 채널이 없어 실패 보고 불가(다음 성공 보고 시 failCount 전달). */
    DlMetaLoad(&meta);
    meta.failCount++;
    if (meta.failCount >= BOOT_DL_FAIL_LIMIT)
    {
        meta.active = 0u;
    }
    DlMetaSave(&meta);
}
