#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"
#include <sqlite3.h>

#define BOT_TOKEN "8948767241:AAGJ_TW1Y8XvZB0UKRDnlEZNw6xaZtlgl_o"
#define API_URL "https://api.telegram.org/bot" BOT_TOKEN "/"

// Структура ответа от libcurl
typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// Отправка HTTP POST запросов к Telegram API
char* http_post(const char *method, const char *json_data) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    MemoryStruct chunk = {malloc(1), 0};
    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_URL, method);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? chunk.memory : NULL;
}

// Инициализация базы данных и заполнение тестовыми автосалонами Армении
void init_database(sqlite3 **db) {
    sqlite3_open("cars_armenia.db", db);
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS offers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "brand TEXT, model TEXT, dealer TEXT, price_amd INT, "
        "warranty_years INT, insurance_amd INT);"
        
        // Очищаем и наполняем базой (в реальном боте наполнение идет из парсера)
        "DELETE FROM offers;"
        "INSERT INTO offers (brand, model, dealer, price_amd, warranty_years, insurance_amd) VALUES "
        "('Toyota', 'RAV4', 'Toyota Yerevan Official', 18500000, 3, 270000),"
        "('Toyota', 'RAV4', 'Auto.am Import', 17200000, 1, 250000),"
        "('Toyota', 'Camry', 'Toyota Yerevan Official', 16000000, 3, 240000),"
        "('Kia', 'Sportage', 'Megna Motors (Kia Armenia)', 15800000, 5, 220000),"
        "('Kia', 'Sportage', 'Global Auto Import', 14900000, 1, 200000),"
        "('Hyundai', 'Tucson', 'Hyundai Armenia', 16200000, 5, 230000);";

    sqlite3_exec(*db, sql, 0, 0, 0);
}

// Поиск предложений и форматирование сравнительного отчета
void send_comparison_report(sqlite3 *db, long long chat_id, const char *brand, const char *model) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "SELECT dealer, price_amd, warranty_years, insurance_amd FROM offers "
             "WHERE brand='%s' AND model='%s' ORDER BY price_amd ASC;", brand, model);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) return;

    char report[2048];
    int offset = snprintf(report, sizeof(report), 
                          "📊 *Сравнение предложений для %s %s в Армении:*\n\n", brand, model);

    int count = 0;
    char best_dealer[64] = "";
    long best_price = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dealer = (const char *)sqlite3_column_text(stmt, 0);
        long price = sqlite3_column_int(stmt, 1);
        int warranty = sqlite3_column_int(stmt, 2);
        long insurance = sqlite3_column_int(stmt, 3);

        if (count == 0) {
            strncpy(best_dealer, dealer, sizeof(best_dealer));
            best_price = price;
        }

        offset += snprintf(report + offset, sizeof(report) - offset,
            "🚘 *Дилер:* %s\n"
            "💰 *Цена:* %ld AMD\n"
            "🛡 *Гарантия:* %d лет\n"
            "📋 *КАСКО:* ~%ld AMD/год\n"
            "-----------------------------------\n",
            dealer, price, warranty, insurance);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        snprintf(report, sizeof(report), "❌ По модели %s %s нет предложений в автосалонах.", brand, model);
    } else {
        snprintf(report + offset, sizeof(report) - offset,
            "💡 *Вывод:* Самая низкая цена у дилера *%s* (%ld AMD).", best_dealer, best_price);
    }

    // Отправка итогового сообщения в Telegram
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "chat_id", chat_id);
    cJSON_AddStringToObject(msg, "text", report);
    cJSON_AddStringToObject(msg, "parse_mode", "Markdown");

    char *json_out = cJSON_PrintUnformatted(msg);
    http_post("sendMessage", json_out);

    free(json_out);
    cJSON_Delete(msg);
}

// Главный цикл обработки Telegram Long Polling
void run_bot(sqlite3 *db) {
    long long last_update_id = 0;
    printf("🤖 Бот запущен и ожидает сообщений...\n");

    while (1) {
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"offset\": %lld, \"timeout\": 10}", last_update_id + 1);

        char *response = http_post("getUpdates", payload);
        if (!response) continue;

        cJSON *json = cJSON_Parse(response);
        if (json) {
            cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
            cJSON *update;

            cJSON_ArrayForEach(update, result) {
                last_update_id = cJSON_GetObjectItemCaseSensitive(update, "update_id")->valuedouble;
                cJSON *message = cJSON_GetObjectItemCaseSensitive(update, "message");

                if (message) {
                    long long chat_id = cJSON_GetObjectItemCaseSensitive(
                        cJSON_GetObjectItemCaseSensitive(message, "chat"), "id")->valuedouble;
                    cJSON *text_obj = cJSON_GetObjectItemCaseSensitive(message, "text");

                    if (text_obj && text_obj->valuestring) {
                        char *text = text_obj->valuestring;

                        if (strcmp(text, "/start") == 0) {
                            send_comparison_report(db, chat_id, "Toyota", "RAV4");
                        }
                    }
                }
            }
            cJSON_Delete(json);
        }
        free(response);
    }
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    
    sqlite3 *db;
    init_database(&db);

    run_bot(db);

    sqlite3_close(db);
    curl_global_cleanup();
    return 0;
}
