#ifndef BOOT_UDP_DL_H
#define BOOT_UDP_DL_H
/* [FW-UPD] 관리서버 UDP 앱 업데이트 부트로더 모듈
 * 플래시 맵: BOOT 24K @0x08000000 / APP 160K @0x08006000 / META 8K @0x0802E000
 *  - META page1(0x0802E000)=BootInfo, page2(0x0802F000)=다운로드 상태(DlMeta)
 * 프레임 포맷(app_nbiot.c DL-ACK 분류와 동일):
 *   [0xA3][msgLen][cmd][payload...][checksum]
 *   - msgLen   = cmd + payload 길이 (전체 길이 - 3)
 *   - checksum = header부터 payload까지 합산 하위 8비트
 *   - msgLen 1바이트 → FW_CHUNK payload는 최대 248B (기존 설계 512B에서 정정)
 * 프로토콜(자체 정의, mgmt DL 6.x 확장):
 *   6.9  FW_BEGIN cmd=0x54 [ver(2)][size(4)][crc32(4)][serverIp(4)][serverPort(2)]
 *        (APP 수신 -> DlMeta 기록(서버 주소 포함) -> SW reset)
 *   6.10 FW_CHUNK  cmd=0x55 [offset(4)][len(2)][payload(len)]  (BOOT 수신 -> 기록 -> ACK)
 *        BOOT ACK  cmd=0x55 [offset(4)][len(2)][status(1)] 에코
 *   6.11 FW_END    cmd=0x56 [crc32(4)]  (검증 -> 플래그 소거 -> 보고 -> 점프)
 *   6.12 FW_REPORT cmd=0x57 [result(1)][failCount(1)][recvOffset(4)]  (BOOT->서버 UL 보고)
 *        result: 0x00=READY(resume 오프셋 안내) 0x01=SUCCESS 0x02=FAIL_CRC
 *                0x03=FAIL_LIMIT 0x04=FAIL_ATTACH
 * EVTIND 없이 운영: CGATT/CEREG로 attach 판정. 실패 반복 -> limit 초과 시 서버 실패 보고. */
#include "stm32l0xx_hal.h"

#define BOOT_DL_APP_BASE        (0x08006000u)
#define BOOT_DL_APP_SIZE        (160u * 1024u)
#define BOOT_DL_META_PAGE2      (0x0802F000u)
#define BOOT_DL_CHUNK_MAX       (248u)   /* msgLen(1B) 한계: cmd(1)+offset(4)+len(2)+payload <= 255 */
#define BOOT_DL_FAIL_LIMIT      (5u)
#define BOOT_DL_UDP_PORT        (6001u)
#define BOOT_DL_ATTACH_TIMEOUT_MS   (60000u)
#define BOOT_DL_IDLE_TIMEOUT_MS     (90000u)   /* 프레임 사이 최대 대기(이후 1회 실패 누적) */
#define BOOT_DL_NET_ATTEMPTS        (2u)       /* 부팅 1회당 attach+socket 재시도 */

#define BOOT_DL_CMD_FW_BEGIN    (0x54u)
#define BOOT_DL_CMD_FW_CHUNK    (0x55u)
#define BOOT_DL_CMD_FW_END      (0x56u)
#define BOOT_DL_CMD_FW_REPORT   (0x57u)

#define BOOT_DL_MAGIC           (0x4657444Cu)  /* "FWDL" */

/* FW_CHUNK ACK status */
#define BOOT_DL_ACK_OK          (0x00u)
#define BOOT_DL_ACK_GAP         (0x01u)   /* 요청 오프셋 != recvOffset (서버 재동기화용) */

/* FW_REPORT result */
#define BOOT_DL_RPT_READY       (0x00u)
#define BOOT_DL_RPT_SUCCESS     (0x01u)
#define BOOT_DL_RPT_FAIL_CRC    (0x02u)
#define BOOT_DL_RPT_FAIL_LIMIT  (0x03u)
#define BOOT_DL_RPT_FAIL_ATTACH (0x04u)

typedef struct
{
    uint32_t magic;        /* BOOT_DL_MAGIC ("FWDL") */
    uint32_t imageSize;    /* 총 이미지 크기 = BootImageHeader_t.firmwareSize (CRC 대상) */
    uint32_t imageCrc;     /* 총 CRC32 (patch @0xC8, BootCRC 유닛 word accumulate) */
    uint32_t recvOffset;   /* 다음 기록 오프셋 (전원차단 재개용) */
    uint16_t versionMajor;
    uint16_t versionMinor;
    uint8_t  serverIp[4];  /* 6.9 수신 시 APP이 기록 (NSOST/NSORF 용) */
    uint16_t serverPort;
    uint8_t  active;       /* 1=다운로드 진행 */
    uint8_t  failCount;    /* 부팅 단위 실패 누적(limit 초과 시 실패 보고 후 idle) */
    uint16_t reserved;
    uint32_t crc32;        /* 구조체 CRC (BootCRC 유닛, word accumulate) */
} BootDlMeta_t;            /* 36 bytes */

/* 진입점: boot_main이 부팅 직후 1회 호출. DlMeta.active!=1이면 즉시 반환(정상 점프).
 * 완료/실패 보고와 플래그 정리를 내부에서 마친 뒤 반환하면 boot_main이 슬롯 검증 후 점프한다. */
void BootUdpDlRun(void);

/* LPUART1_IRQHandler에서 호출 (boot_stm32l0xx_it.c) */
void BootUdpDlUartIrq(void);

#endif /* BOOT_UDP_DL_H */
