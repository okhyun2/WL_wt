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

    APP_LOGI("MSTOR", "save count=%u", App_MeterStorageCount());

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
