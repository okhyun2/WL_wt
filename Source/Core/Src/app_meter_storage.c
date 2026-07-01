#include "app_meter_storage.h"

#include <string.h>

#include "app_build_config.h"
#include "app_log.h"

typedef struct APP_METER_STORAGE_PACKED
{
    uint32_t magic;
    uint8_t version;
    uint8_t head;
    uint8_t count;
    uint8_t reserved0;
    uint16_t nextSeq;
    uint16_t metaSeq;
    uint16_t crc16;
    uint8_t reserved1[2];
} AppMeterStorageMetaSlot_t;

enum
{
    APP_METER_STORAGE_META_REGION_SIZE = (APP_METER_STORAGE_META_SLOT_COUNT * (uint32_t)sizeof(AppMeterStorageMetaSlot_t)),
    APP_METER_STORAGE_RECORD_REGION_OFFSET = APP_METER_STORAGE_META_REGION_SIZE,
    APP_METER_STORAGE_RECORD_SLOT_SIZE = (uint32_t)sizeof(AppMeterStorageRecord_t),
    APP_METER_STORAGE_RECORD_REGION_SIZE = (APP_METER_STORAGE_MAX_RECORDS * APP_METER_STORAGE_RECORD_SLOT_SIZE)
};

typedef char AppMeterStorageLayoutCheck_t[
    ((APP_METER_STORAGE_RECORD_REGION_OFFSET + APP_METER_STORAGE_RECORD_REGION_SIZE) <= APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES) ? 1 : -1];

static struct
{
    AppMeterStorageInfo_t info;
    uint16_t metaSeq;
    uint8_t metaSlotIndex;
} g_appMeterStorageContext;

static uint16_t App_MeterStorageCrc16Ccitt(const uint8_t *p_data, uint32_t length)
{
    uint16_t crc = 0xFFFFu;
    uint32_t index;
    uint8_t bit;

    for (index = 0u; index < length; index++)
    {
        crc ^= ((uint16_t)p_data[index] << 8);
        for (bit = 0u; bit < 8u; bit++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t App_MeterStorageCalcRecordCrc8(const AppMeterStorageRecord_t *p_record)
{
    const uint8_t *p_bytes;
    uint8_t crc;
    uint32_t index;

    p_bytes = (const uint8_t *)p_record;
    crc = 0u;
    for (index = 0u; index < (sizeof(AppMeterStorageRecord_t) - 2u); index++)
    {
        crc ^= p_bytes[index];
    }

    return crc;
}

static uint32_t App_MeterStorageGetRecordOffset(uint8_t physicalIndex)
{
    return (APP_METER_STORAGE_RECORD_REGION_OFFSET + ((uint32_t)physicalIndex * APP_METER_STORAGE_RECORD_SLOT_SIZE));
}

static uint8_t App_MeterStorageGetPhysicalIndex(uint8_t logicalIndex)
{
    return (uint8_t)((g_appMeterStorageContext.info.head + logicalIndex) % APP_METER_STORAGE_MAX_RECORDS);
}

static uint8_t App_MeterStorageIsMetaValid(const AppMeterStorageMetaSlot_t *p_slot)
{
    AppMeterStorageMetaSlot_t temp;
    uint16_t crc;

    if (p_slot == NULL)
    {
        return APP_FALSE;
    }

    if ((p_slot->magic != APP_METER_STORAGE_MAGIC) || (p_slot->version != APP_METER_STORAGE_VERSION))
    {
        return APP_FALSE;
    }

    if ((p_slot->head >= APP_METER_STORAGE_MAX_RECORDS) || (p_slot->count > APP_METER_STORAGE_MAX_RECORDS))
    {
        return APP_FALSE;
    }

    temp = *p_slot;
    temp.crc16 = 0u;
    crc = App_MeterStorageCrc16Ccitt((const uint8_t *)&temp, sizeof(temp));
    return (crc == p_slot->crc16) ? APP_TRUE : APP_FALSE;
}

static AppStatus_t App_MeterStorageCommitMeta(void)
{
    AppMeterStorageMetaSlot_t slot;
    uint32_t offset;

    (void)memset(&slot, 0, sizeof(slot));
    slot.magic = APP_METER_STORAGE_MAGIC;
    slot.version = APP_METER_STORAGE_VERSION;
    slot.head = g_appMeterStorageContext.info.head;
    slot.count = g_appMeterStorageContext.info.count;
    slot.nextSeq = g_appMeterStorageContext.info.nextSeq;
    slot.metaSeq = (uint16_t)(g_appMeterStorageContext.metaSeq + 1u);
    slot.crc16 = 0u;
    slot.crc16 = App_MeterStorageCrc16Ccitt((const uint8_t *)&slot, sizeof(slot));

    g_appMeterStorageContext.metaSlotIndex = (uint8_t)((g_appMeterStorageContext.metaSlotIndex + 1u) % APP_METER_STORAGE_META_SLOT_COUNT);
    offset = ((uint32_t)g_appMeterStorageContext.metaSlotIndex) * sizeof(AppMeterStorageMetaSlot_t);

    APP_RETURN_IF_FALSE(App_StorageDataEepromWrite(offset, &slot, sizeof(slot)) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    g_appMeterStorageContext.metaSeq = slot.metaSeq;
    return APP_STATUS_OK;
}

static AppStatus_t App_MeterStorageLoadLatestMeta(void)
{
    AppMeterStorageMetaSlot_t slot;
    AppMeterStorageMetaSlot_t bestSlot;
    uint8_t found;
    uint32_t slotIndex;
    uint32_t offset;

    found = APP_FALSE;
    (void)memset(&bestSlot, 0, sizeof(bestSlot));

    for (slotIndex = 0u; slotIndex < APP_METER_STORAGE_META_SLOT_COUNT; slotIndex++)
    {
        offset = slotIndex * sizeof(AppMeterStorageMetaSlot_t);
        if (App_StorageDataEepromRead(offset, &slot, sizeof(slot)) != APP_STATUS_OK)
        {
            continue;
        }

        if (App_MeterStorageIsMetaValid(&slot) != APP_TRUE)
        {
            continue;
        }

        //이미 찾은 최신 슬롯(bestSlot)과 새로 읽은 슬롯(slot)의 시퀀스 번호를 비교하여, 새로 읽은 슬롯이 더 최신 데이터인지를 판별
        if ((found == APP_FALSE) || ((uint16_t)(slot.metaSeq - bestSlot.metaSeq) < 0x8000u))
        {
            bestSlot = slot;
            g_appMeterStorageContext.metaSlotIndex = (uint8_t)slotIndex;
            found = APP_TRUE;
        }
    }

    if (found == APP_FALSE)
    {
        g_appMeterStorageContext.info.head = 0u;
        g_appMeterStorageContext.info.count = 0u;
        g_appMeterStorageContext.info.nextSeq = 1u;
        g_appMeterStorageContext.metaSeq = 0u;
        return APP_STATUS_OK;
    }

    g_appMeterStorageContext.info.head = bestSlot.head;
    g_appMeterStorageContext.info.count = bestSlot.count;
    g_appMeterStorageContext.info.nextSeq = bestSlot.nextSeq;
    g_appMeterStorageContext.metaSeq = bestSlot.metaSeq;
    return APP_STATUS_OK;
}

static AppStatus_t App_MeterStorageReadPhysical(uint8_t physicalIndex, AppMeterStorageRecord_t *p_record)
{
    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);
    return App_StorageDataEepromRead(App_MeterStorageGetRecordOffset(physicalIndex), p_record, sizeof(*p_record));
}

static AppStatus_t App_MeterStorageWritePhysical(uint8_t physicalIndex, const AppMeterStorageRecord_t *p_record)
{
    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);
    return App_StorageDataEepromWrite(App_MeterStorageGetRecordOffset(physicalIndex), p_record, sizeof(*p_record));
}

AppStatus_t App_StorageDataEepromRead(uint32_t offset, void *p_data, uint32_t sizeBytes)
{
#ifdef SUPPORT_EEPROM
    uint32_t address;

    APP_RETURN_IF_FALSE(p_data != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) >= offset, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) <= APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES, APP_STATUS_INVALID_PARAM);

    address = DATA_EEPROM_BASE + APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES + offset;
    (void)memcpy(p_data, (const void *)address, sizeBytes);
    return APP_STATUS_OK;
#else
    (void)offset;
    (void)p_data;
    (void)sizeBytes;
    return APP_STATUS_NOT_INITIALIZED;
#endif
}

AppStatus_t App_StorageDataEepromWrite(uint32_t offset, const void *p_data, uint32_t sizeBytes)
{
#ifdef SUPPORT_EEPROM
    const uint8_t *p_bytes;
    uint32_t address;
    uint32_t index;

    APP_RETURN_IF_FALSE(p_data != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) >= offset, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) <= APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES, APP_STATUS_INVALID_PARAM);

    p_bytes = (const uint8_t *)p_data;
    address = DATA_EEPROM_BASE + APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES + offset;

    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Unlock(), APP_STATUS_INIT_FAILED);
    for (index = 0u; index < sizeBytes; index++)
    {
        if (HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_BYTE,
                                           address + index,
                                           p_bytes[index]) != HAL_OK)
        {
            (void)HAL_FLASHEx_DATAEEPROM_Lock();
            App_ErrorRecord(APP_STATUS_INIT_FAILED, __FILE__, __LINE__);
            return APP_STATUS_INIT_FAILED;
        }
    }
    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Lock(), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
#else
    (void)offset;
    (void)p_data;
    (void)sizeBytes;
    return APP_STATUS_NOT_INITIALIZED;
#endif
}

void App_MeterStorageInfo(void)
{
    APP_LOGI("STOR", "EEPROM save map: address:0x%08lx, size:%lubyte",
             DATA_EEPROM_BASE + APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES,
             APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES);
    APP_LOGI("STOR", "  meta: address:0x%08lx, size:%lubyte, slot(count:%d, size:%lubyte)",
             DATA_EEPROM_BASE + APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES,
             APP_METER_STORAGE_META_REGION_SIZE,
             APP_METER_STORAGE_META_SLOT_COUNT,
             (uint32_t)sizeof(AppMeterStorageMetaSlot_t));

    APP_LOGI("STOR", "  record: address:0x%08lx, size:%lubyte, slot(count:%d, size:%lubyte)",
             DATA_EEPROM_BASE + APP_METER_STORAGE_RECORD_REGION_OFFSET,
             APP_METER_STORAGE_RECORD_REGION_SIZE,
             APP_METER_STORAGE_MAX_RECORDS,
             APP_METER_STORAGE_RECORD_SLOT_SIZE);
}

AppStatus_t App_MeterStorageInit(void)
{
    (void)memset(&g_appMeterStorageContext, 0, sizeof(g_appMeterStorageContext));
    APP_RETURN_IF_FALSE(App_MeterStorageLoadLatestMeta() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    g_appMeterStorageContext.info.initialized = APP_TRUE;

    if (g_appMeterStorageContext.metaSeq == 0u)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageCommitMeta() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    return APP_STATUS_OK;
}

uint8_t App_MeterStorageIsInitialized(void)
{
    return g_appMeterStorageContext.info.initialized;
}

AppStatus_t App_MeterStoragePush(const AppMeterStorageRecord_t *p_record)
{
    AppMeterStorageRecord_t temp;
    uint8_t writeIndex;

    APP_LOGI("MSTOR", "current saved count=%u/%u(%s)", App_MeterStorageCount(), APP_METER_STORAGE_MAX_RECORDS,
        (App_MeterStorageCount() == APP_METER_STORAGE_MAX_RECORDS) ? "rolling":"static");

    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    temp = *p_record;
    temp.flags |= APP_METER_STORAGE_FLAG_VALID;
    temp.seq = g_appMeterStorageContext.info.nextSeq;
    temp.crc8 = 0u;
    temp.crc8 = App_MeterStorageCalcRecordCrc8(&temp);

    if (g_appMeterStorageContext.info.count < APP_METER_STORAGE_MAX_RECORDS)
    {
        writeIndex = App_MeterStorageGetPhysicalIndex(g_appMeterStorageContext.info.count);
        APP_RETURN_IF_FALSE(App_MeterStorageWritePhysical(writeIndex, &temp) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        g_appMeterStorageContext.info.count++;
    }
    else
    {
        writeIndex = g_appMeterStorageContext.info.head;
        APP_RETURN_IF_FALSE(App_MeterStorageWritePhysical(writeIndex, &temp) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        g_appMeterStorageContext.info.head = (uint8_t)((g_appMeterStorageContext.info.head + 1u) % APP_METER_STORAGE_MAX_RECORDS);
    }

    g_appMeterStorageContext.info.nextSeq++;
    APP_RETURN_IF_FALSE(App_MeterStorageCommitMeta() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);

    APP_LOGI("MSTOR", "save count=%u/%u(%s)", App_MeterStorageCount(), APP_METER_STORAGE_MAX_RECORDS,
        (App_MeterStorageCount() == APP_METER_STORAGE_MAX_RECORDS) ? "rolling":"static");

    return APP_STATUS_OK;
}

AppStatus_t App_MeterStoragePeekOldest(AppMeterStorageRecord_t *p_record)
{
    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE(g_appMeterStorageContext.info.count != 0u, APP_STATUS_NOT_INITIALIZED);
    return App_MeterStorageReadPhysical(g_appMeterStorageContext.info.head, p_record);
}

AppStatus_t App_MeterStorageReadAt(uint8_t logicalIndex, AppMeterStorageRecord_t *p_record)
{
    uint8_t physicalIndex;

    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(logicalIndex < g_appMeterStorageContext.info.count, APP_STATUS_INVALID_PARAM);

    physicalIndex = App_MeterStorageGetPhysicalIndex(logicalIndex);
    APP_RETURN_IF_FALSE(App_MeterStorageReadPhysical(physicalIndex, p_record) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(p_record->crc8 == App_MeterStorageCalcRecordCrc8(p_record), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_MeterStorageDeleteOldest(uint8_t deleteCount)
{
    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE(deleteCount != 0u, APP_STATUS_INVALID_PARAM);

    if (deleteCount >= g_appMeterStorageContext.info.count)
    {
        g_appMeterStorageContext.info.head = 0u;
        g_appMeterStorageContext.info.count = 0u;
    }
    else
    {
        g_appMeterStorageContext.info.head = (uint8_t)((g_appMeterStorageContext.info.head + deleteCount) % APP_METER_STORAGE_MAX_RECORDS);
        g_appMeterStorageContext.info.count = (uint8_t)(g_appMeterStorageContext.info.count - deleteCount);
    }

    APP_RETURN_IF_FALSE(App_MeterStorageCommitMeta() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_MeterStorageMarkOldestSent(uint8_t sentCount)
{
    AppMeterStorageRecord_t record;
    uint8_t logicalIndex;
    uint8_t physicalIndex;

    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE((sentCount != 0u) && (sentCount <= g_appMeterStorageContext.info.count), APP_STATUS_INVALID_PARAM);

    for (logicalIndex = 0u; logicalIndex < sentCount; logicalIndex++)
    {
        physicalIndex = App_MeterStorageGetPhysicalIndex(logicalIndex);
        APP_RETURN_IF_FALSE(App_MeterStorageReadPhysical(physicalIndex, &record) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        record.flags |= APP_METER_STORAGE_FLAG_SENT;
        record.crc8 = 0u;
        record.crc8 = App_MeterStorageCalcRecordCrc8(&record);
        APP_RETURN_IF_FALSE(App_MeterStorageWritePhysical(physicalIndex, &record) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    return APP_STATUS_OK;
}

uint8_t App_MeterStorageCount(void)
{
    return g_appMeterStorageContext.info.count;
}

AppStatus_t App_MeterStorageClearAll(void)
{
    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    g_appMeterStorageContext.info.head = 0u;
    g_appMeterStorageContext.info.count = 0u;
    APP_RETURN_IF_FALSE(App_MeterStorageCommitMeta() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_MeterStorageGetInfo(AppMeterStorageInfo_t *p_info)
{
    if (App_MeterStorageIsInitialized() != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStorageInit() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }

    APP_RETURN_IF_FALSE(p_info != NULL, APP_STATUS_INVALID_PARAM);
    *p_info = g_appMeterStorageContext.info;
    return APP_STATUS_OK;
}

/* ================================================================
 *  Bank1 Low-level EEPROM I/O
 * ================================================================ */
/* Bank1 전체(CONFIG + OPTION + RESERVED = 3KB) 범위 내 임의 offset 접근 */
#define APP_STORAGE_BANK1_SIZE_BYTES    (APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES + \
                                         APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES + \
                                         APP_STORAGE_RESERVED_EEPROM_SIZE_BYTES)

AppStatus_t App_StorageConfigEepromRead(uint32_t offset, void *p_data, uint32_t sizeBytes)
{
#ifdef SUPPORT_EEPROM
    uint32_t address;

    APP_RETURN_IF_FALSE(p_data != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) >= offset, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) <= APP_STORAGE_BANK1_SIZE_BYTES,
                        APP_STATUS_INVALID_PARAM);

    address = DATA_EEPROM_BASE + offset;     /* Bank1 시작 = DATA_EEPROM_BASE + 0 */
    (void)memcpy(p_data, (const void *)address, sizeBytes);
    return APP_STATUS_OK;
#else
    (void)offset; (void)p_data; (void)sizeBytes;
    return APP_STATUS_NOT_INITIALIZED;
#endif
}

AppStatus_t App_StorageConfigEepromWrite(uint32_t offset, const void *p_data, uint32_t sizeBytes)
{
#ifdef SUPPORT_EEPROM
    const uint8_t *p_bytes;
    uint32_t address;
    uint32_t i;

    APP_RETURN_IF_FALSE(p_data != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) >= offset, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((offset + sizeBytes) <= APP_STORAGE_BANK1_SIZE_BYTES,
                        APP_STATUS_INVALID_PARAM);

    p_bytes = (const uint8_t *)p_data;
    address = DATA_EEPROM_BASE + offset;

    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Unlock(), APP_STATUS_INIT_FAILED);
    for (i = 0u; i < sizeBytes; i++)
    {
        if (*(volatile uint8_t *)(address + i) == p_bytes[i])
        {
            continue;
        }
        if (HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_BYTE,
                                           address + i,
                                           p_bytes[i]) != HAL_OK)
        {
            (void)HAL_FLASHEx_DATAEEPROM_Lock();
            App_ErrorRecord(APP_STATUS_INIT_FAILED, __FILE__, __LINE__);
            return APP_STATUS_INIT_FAILED;
        }
    }
    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Lock(), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
#else
    (void)offset; (void)p_data; (void)sizeBytes;
    return APP_STATUS_NOT_INITIALIZED;
#endif
}

/* ================================================================
 *  공용 슬롯 wear-leveling 헬퍼
 * ================================================================ */
#define APP_CONFIG_SLOT_MAGIC           (0x53434647u)   /* 'SCFG' */
#define APP_CONFIG_SLOT_VERSION         (0x01u)
#define APP_CONFIG_SLOT_PAYLOAD_MAX     (256u)

typedef struct APP_METER_STORAGE_PACKED
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  payloadSize;
    uint16_t seq;
    uint16_t crc16;
    uint8_t  reserved[2];
} AppConfigSlotHeader_t;

static uint32_t App_ConfigSlotStride(const AppConfigSlotRegion_t *p_region)
{
    return (p_region->regionSize / p_region->slotCount);
}

static AppStatus_t App_ConfigSlotReadSlot(const AppConfigSlotRegion_t *p_region,
                                          uint8_t slotIndex,
                                          AppConfigSlotHeader_t *p_header,
                                          void *p_payload)
{
    uint32_t base = p_region->regionOffset +
                    ((uint32_t)slotIndex * App_ConfigSlotStride(p_region));

    APP_RETURN_IF_FALSE(App_StorageConfigEepromRead(base, p_header,
                        sizeof(*p_header)) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_StorageConfigEepromRead(base + sizeof(*p_header),
                        p_payload, p_region->payloadSize) == APP_STATUS_OK,
                        APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_ConfigSlotWriteSlot(const AppConfigSlotRegion_t *p_region,
                                           uint8_t slotIndex,
                                           const AppConfigSlotHeader_t *p_header,
                                           const void *p_payload)
{
    uint32_t base = p_region->regionOffset +
                    ((uint32_t)slotIndex * App_ConfigSlotStride(p_region));

    APP_RETURN_IF_FALSE(App_StorageConfigEepromWrite(base, p_header,
                        sizeof(*p_header)) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_StorageConfigEepromWrite(base + sizeof(*p_header),
                        p_payload, p_region->payloadSize) == APP_STATUS_OK,
                        APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static uint8_t App_ConfigSlotIsValid(const AppConfigSlotHeader_t *p_header,
                                     const void *p_payload,
                                     uint8_t expectedPayloadSize)
{
    AppConfigSlotHeader_t tmp;
    uint8_t  buf[sizeof(AppConfigSlotHeader_t) + APP_CONFIG_SLOT_PAYLOAD_MAX];
    uint16_t crc;

    if ((p_header->magic != APP_CONFIG_SLOT_MAGIC) ||
        (p_header->version != APP_CONFIG_SLOT_VERSION) ||
        (p_header->payloadSize != expectedPayloadSize) ||
        (expectedPayloadSize > APP_CONFIG_SLOT_PAYLOAD_MAX))
    {
        return APP_FALSE;
    }

    tmp = *p_header;
    tmp.crc16 = 0u;
    (void)memcpy(buf, &tmp, sizeof(tmp));
    (void)memcpy(&buf[sizeof(tmp)], p_payload, expectedPayloadSize);

    crc = App_MeterStorageCrc16Ccitt(buf, sizeof(tmp) + expectedPayloadSize);
    return (crc == p_header->crc16) ? APP_TRUE : APP_FALSE;
}

static AppStatus_t App_ConfigSlotFindLatest(AppConfigSlotRegion_t *p_region,
                                            void *p_payloadOut,
                                            uint8_t *p_foundOut)
{
    AppConfigSlotHeader_t header;
    AppConfigSlotHeader_t bestHeader;
    uint8_t  payload[APP_CONFIG_SLOT_PAYLOAD_MAX];
    uint8_t  bestPayload[APP_CONFIG_SLOT_PAYLOAD_MAX];
    uint8_t  found = APP_FALSE;
    uint8_t  i;

    APP_RETURN_IF_FALSE(p_region->payloadSize <= APP_CONFIG_SLOT_PAYLOAD_MAX,
                        APP_STATUS_INVALID_PARAM);

    (void)memset(&bestHeader, 0, sizeof(bestHeader));

    for (i = 0u; i < p_region->slotCount; i++)
    {
        if (App_ConfigSlotReadSlot(p_region, i, &header, payload) != APP_STATUS_OK)
        {
            continue;
        }
        if (App_ConfigSlotIsValid(&header, payload, p_region->payloadSize) != APP_TRUE)
        {
            continue;
        }
        if ((found == APP_FALSE) ||
            ((uint16_t)(header.seq - bestHeader.seq) < 0x8000u))
        {
            bestHeader = header;
            (void)memcpy(bestPayload, payload, p_region->payloadSize);
            p_region->latestSlotIndex = i;
            found = APP_TRUE;
        }
    }

    if (found == APP_TRUE)
    {
        (void)memcpy(p_payloadOut, bestPayload, p_region->payloadSize);
        p_region->latestSeq = bestHeader.seq;
    }
    *p_foundOut = found;
    return APP_STATUS_OK;
}

AppStatus_t App_ConfigSlotInit(AppConfigSlotRegion_t *p_region)
{
    uint8_t scratch[APP_CONFIG_SLOT_PAYLOAD_MAX];
    uint8_t found;

    APP_RETURN_IF_FALSE(p_region != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_region->slotCount > 0u, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_region->payloadSize <= APP_CONFIG_SLOT_PAYLOAD_MAX,
                        APP_STATUS_INVALID_PARAM);

    APP_RETURN_IF_FALSE(App_ConfigSlotFindLatest(p_region, scratch, &found)
                        == APP_STATUS_OK, APP_STATUS_INIT_FAILED);

    if (found == APP_FALSE)
    {
        p_region->latestSlotIndex = (uint8_t)(p_region->slotCount - 1u);
        p_region->latestSeq       = 0u;
    }
    p_region->initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_ConfigSlotLoad(AppConfigSlotRegion_t *p_region,
                               void *p_payload, uint8_t *p_found)
{
    APP_RETURN_IF_FALSE((p_region != NULL) && (p_payload != NULL) && (p_found != NULL),
                        APP_STATUS_INVALID_PARAM);
    if (p_region->initialized != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_ConfigSlotInit(p_region) == APP_STATUS_OK,
                            APP_STATUS_INIT_FAILED);
    }
    return App_ConfigSlotFindLatest(p_region, p_payload, p_found);
}

AppStatus_t App_ConfigSlotSave(AppConfigSlotRegion_t *p_region, const void *p_payload)
{
    AppConfigSlotHeader_t header;
    uint8_t  buf[sizeof(AppConfigSlotHeader_t) + APP_CONFIG_SLOT_PAYLOAD_MAX];
    uint8_t  nextSlot;

    APP_RETURN_IF_FALSE((p_region != NULL) && (p_payload != NULL),
                        APP_STATUS_INVALID_PARAM);
    if (p_region->initialized != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_ConfigSlotInit(p_region) == APP_STATUS_OK,
                            APP_STATUS_INIT_FAILED);
    }

    nextSlot = (uint8_t)((p_region->latestSlotIndex + 1u) % p_region->slotCount);

    (void)memset(&header, 0, sizeof(header));
    header.magic       = APP_CONFIG_SLOT_MAGIC;
    header.version     = APP_CONFIG_SLOT_VERSION;
    header.payloadSize = p_region->payloadSize;
    header.seq         = (uint16_t)(p_region->latestSeq + 1u);
    header.crc16       = 0u;

    (void)memcpy(buf, &header, sizeof(header));
    (void)memcpy(&buf[sizeof(header)], p_payload, p_region->payloadSize);
    header.crc16 = App_MeterStorageCrc16Ccitt(buf,
                       sizeof(header) + p_region->payloadSize);

    APP_RETURN_IF_FALSE(App_ConfigSlotWriteSlot(p_region, nextSlot, &header, p_payload)
                        == APP_STATUS_OK, APP_STATUS_INIT_FAILED);

    p_region->latestSlotIndex = nextSlot;
    p_region->latestSeq       = header.seq;
    return APP_STATUS_OK;
}

AppStatus_t App_ConfigSlotClear(AppConfigSlotRegion_t *p_region)
{
    uint8_t  zero = 0x00u;
    uint32_t i;

    APP_RETURN_IF_FALSE(p_region != NULL, APP_STATUS_INVALID_PARAM);

    for (i = 0u; i < p_region->regionSize; i++)
    {
        APP_RETURN_IF_FALSE(App_StorageConfigEepromWrite(
                                p_region->regionOffset + i, &zero, 1u)
                            == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    }
    p_region->latestSlotIndex = (uint8_t)(p_region->slotCount - 1u);
    p_region->latestSeq       = 0u;
    return APP_STATUS_OK;
}

/* ================================================================
 *  Device Config (Bank1)
 * ================================================================ */
static AppConfigSlotRegion_t g_appDeviceConfigRegion =
{
    .regionOffset    = APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES,   /* 0 */
    .regionSize      = APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES,     /* 1024 */
    .slotCount       = APP_STORAGE_CONFIG_SLOT_COUNT,                    /* 4 */
    .payloadSize     = (uint8_t)sizeof(AppDeviceConfig_t),
    .latestSlotIndex = 0u,
    .latestSeq       = 0u,
    .initialized     = APP_FALSE,
};

void App_DeviceConfigSetDefaults(AppDeviceConfig_t *p_config)
{
    if (p_config == NULL) { return; }

    (void)memset(p_config, 0, sizeof(*p_config));
    p_config->bootCountValid = 1u;
    p_config->bootCount      = 0u;
    p_config->logLevel       = 3u;
    p_config->linkType       = 1u;
}

AppStatus_t App_DeviceConfigInit(void)
{
    return App_ConfigSlotInit(&g_appDeviceConfigRegion);
}

AppStatus_t App_DeviceConfigLoad(AppDeviceConfig_t *p_config)
{
    uint8_t found;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_config != NULL, APP_STATUS_INVALID_PARAM);

    status = App_ConfigSlotLoad(&g_appDeviceConfigRegion, p_config, &found);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    if (found == APP_FALSE)
    {
        App_DeviceConfigSetDefaults(p_config);
        return APP_STATUS_NOT_INITIALIZED;
    }
    return APP_STATUS_OK;
}

AppStatus_t App_DeviceConfigSave(const AppDeviceConfig_t *p_config)
{
    APP_RETURN_IF_FALSE(p_config != NULL, APP_STATUS_INVALID_PARAM);
    return App_ConfigSlotSave(&g_appDeviceConfigRegion, p_config);
}

AppStatus_t App_DeviceConfigClear(void)
{
    return App_ConfigSlotClear(&g_appDeviceConfigRegion);
}

void App_DeviceConfigInfo(void)
{
    APP_LOGI("CFG", "Bank1 device cfg: addr=0x%08lx, size=%lu, slot(cnt=%u, payload=%lu)",
             (uint32_t)(DATA_EEPROM_BASE + APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES),
             (uint32_t)APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES,
             (unsigned)APP_STORAGE_CONFIG_SLOT_COUNT,
             (uint32_t)sizeof(AppDeviceConfig_t));
}

void App_DeviceConfigDump(const AppDeviceConfig_t *p_config)
{
    uint32_t i;

    if (p_config == NULL)
    {
        APP_LOGW("CFG", "dump: null");
        return;
    }

    APP_LOGI("CFG", "===== Device Config =====");
    APP_LOGI("CFG", "  bootCountValid : %u", (unsigned)p_config->bootCountValid);
    APP_LOGI("CFG", "  bootCount      : %lu", (uint32_t)p_config->bootCount);
    APP_LOGI("CFG", "  logLevel       : %u", (unsigned)p_config->logLevel);
    APP_LOGI("CFG", "  linkType       : %u (%s)",
             (unsigned)p_config->linkType,
             (p_config->linkType == 0u) ? "LoRa" :
             (p_config->linkType == 1u) ? "NB-IoT" : "unknown");

    /* reserved 영역 중 0이 아닌 바이트만 표시 (디버깅용) */
    for (i = 0u; i < sizeof(p_config->reserved); i++)
    {
        if (p_config->reserved[i] != 0u)
        {
            APP_LOGI("CFG", "  reserved[%lu]   : 0x%02X", i, p_config->reserved[i]);
        }
    }

    APP_LOGI("CFG", "  slot(idx=%u, seq=%u)",
             (unsigned)g_appDeviceConfigRegion.latestSlotIndex,
             (unsigned)g_appDeviceConfigRegion.latestSeq);
}
