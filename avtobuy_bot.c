#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"
#include <sqlite3.h>

#define BOT_TOKEN "8948767241:AAGJ_TW1Y8XvZB0UKRDnlEZNw6xaZtlgl_o"
#define API_URL "https://api.telegram.org/bot" BOT_TOKEN "/"

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

// Структура для пошагового мастера (Wizard)
typedef struct UserState {
    long long chat_id;
    int step; // 0 - свободен, 1 - ждем марку, 2 - ждем модель, 3 - ждем год
    char brand[64];
    char model[64];
    int year;
    struct UserState *next;
} UserState;

UserState *users_head = NULL;

UserState* get_user_state(long long chat_id) {
    UserState *curr = users_head;
    while (curr) {
        if (curr->chat_id == chat_id) return curr;
        curr = curr->next;
    }
    UserState *new_user = malloc(sizeof(UserState));
    new_user->chat_id = chat_id;
    new_user->step = 0;
    new_user->next = users_head;
    users_head = new_user;
    return new_user;
}

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

void send_text_message(long long chat_id, const char *text) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "chat_id", chat_id);
    cJSON_AddStringToObject(msg, "text", text);
    cJSON_AddStringToObject(msg, "parse_mode", "Markdown");
    cJSON_AddBoolToObject(msg, "disable_web_page_preview", 1);

    char *json_out = cJSON_PrintUnformatted(msg);
    http_post("sendMessage", json_out);

    free(json_out);
    cJSON_Delete(msg);
}

// Инициализация базы данных с официальными дилерами Армении
void init_database(sqlite3 **db) {
    sqlite3_open("cars_armenia.db", db);
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS offers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "brand TEXT, model TEXT, year INT, dealer TEXT, price_amd INT, "
        "warranty_years INT, insurance_amd INT, dealer_url TEXT);"
        
        "DELETE FROM offers;"
        "INSERT INTO offers (brand, model, year, dealer, price_amd, warranty_years, insurance_amd, dealer_url) VALUES "
        // Mercedes-Benz (Avantgarde Motors / Official)
        "('Mercedes-Benz', 'S-Class', 2023, 'Mercedes-Benz Armenia (Avantgarde Motors)', 65000000, 2, 950000, 'https://www.mercedes-benz.am'),"
        "('Mercedes-Benz', 'E-Class', 2022, 'Mercedes-Benz Armenia (Avantgarde Motors)', 35000000, 2, 550000, 'https://www.mercedes-benz.am'),"
        // BMW
        "('BMW', 'M5', 2023, 'BMW Armenia Official', 48000000, 3, 700000, 'https://www.bmw.am'),"
        "('BMW', 'X5', 2023, 'BMW Armenia Official', 42000000, 3, 600000, 'https://www.bmw.am'),"
        // Toyota
        "('Toyota', 'RAV4', 2023, 'Toyota Yerevan Official', 18500000, 3, 270000, 'https://www.toyota.am'),"
        "('Toyota', 'RAV4', 2022, 'Auto.am Import (Армения)', 16200000, 1, 230000, 'https://www.auto.am');";

    sqlite3_exec(*db, sql, 0, 0, 0);
}

// Исправленный поиск с сортировкой по близости года (без ошибок, если год отличается)
void send_car_report(sqlite3 *db, long long chat_id, const char *brand, const char *model, int year) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
             "SELECT dealer, year, price_amd, warranty_years, insurance_amd, dealer_url FROM offers "
             "WHERE brand LIKE '%%%s%%' AND model LIKE '%%%s%%' ORDER BY ABS(year - %d) ASC;", 
             brand, model, year);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        send_text_message(chat_id, "❌ Ошибка при поиске в базе данных.");
        return;
    }

    char report[3072];
    int offset = snprintf(report, sizeof(report), 
                          "🇦🇲 *Анализ рынка Армении*\n"
                          "🚗 *Запрос:* %s %s (%d г.)\n\n", brand, model, year);

    int count = 0;
    char best_dealer[128] = "";
    long best_price = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dealer = (const char *)sqlite3_column_text(stmt, 0);
        int db_year = sqlite3_column_int(stmt, 1);
        long price = sqlite3_column_int(stmt, 2);
        int warranty = sqlite3_column_int(stmt, 3);
        long insurance = sqlite3_column_int(stmt, 4);
        const char *url = (const char *)sqlite3_column_text(stmt, 5);

        if (count == 0) {
            strncpy(best_dealer, dealer, sizeof(best_dealer));
            best_price = price;
        }

        offset += snprintf(report + offset, sizeof(report) - offset,
            "🏢 *Дилер / Салон:* %s\n"
            "📅 *Год в наличии:* %d\n"
            "💰 *Цена:* %ld AMD\n"
            "🛡 *Гарантия:* %d лет\n"
            "📋 *КАСКО:* ~%ld AMD/год\n"
            "🌐 [Официальный сайт / Площадка](%s)\n"
            "-----------------------------------\n",
            dealer, db_year, price, warranty, insurance, url ? url : "https://www.auto.am");
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        snprintf(report, sizeof(report), 
                 "❌ По запросу *%s %s (%d г.)* в Армении актуальных предложений у дилеров не найдено.", 
                 brand, model, year);
    } else {
        snprintf(report + offset, sizeof(report) - offset,
            "💡 *Рекомендация:* Ближайшее выгодное предложение у дилера *%s* (`%ld AMD`).", best_dealer, best_price);
    }

    send_text_message(chat_id, report);
}

// Главный цикл с обработкой шагов мастера
void run_bot(sqlite3 *db) {
    long long last_update_id = 0;
    printf("🤖 Бот CarBuy AM запущен и ждет запросы...\n");

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
                        UserState *state = get_user_state(chat_id);

                        if (strcmp(text, "/start") == 0) {
                            state->step = 1; // Шаг 1: ждем марку
                            send_text_message(chat_id, "🚗 Добро пожаловать в *CarBuy AM*!\n\nВведите **марку** машины (например, `Mercedes-Benz`, `BMW`, `Toyota`):");
                        } 
                        else if (state->step == 1) {
                            strncpy(state->brand, text, sizeof(state->brand) - 1);
                            state->step = 2; // Шаг 2: ждем модель
                            
                            char prompt_buf[128];
                            snprintf(prompt_buf, sizeof(prompt_buf), "✅ Марка: *%s*\n\nТеперь введите **модель** (например, `S-Class`, `M5`, `RAV4`):", state->brand);
                            send_text_message(chat_id, prompt_buf);
                        }
                        else if (state->step == 2) {
                            strncpy(state->model, text, sizeof(state->model) - 1);
                            state->step = 3; // Шаг 3: ждем год
                            
                            char prompt_buf[128];
                            snprintf(prompt_buf, sizeof(prompt_buf), "✅ Модель: *%s*\n\nВведите **год производства** (например, `2023`):", state->model);
                            send_text_message(chat_id, prompt_buf);
                        }
                        else if (state->step == 3) {
                            state->year = atoi(text);
                            state->step = 0; // Сбрасываем мастер

                            send_car_report(db, chat_id, state->brand, state->model, state->year);
                        }
                        else {
                            send_text_message(chat_id, "Введите /start для нового поиска автомобиля.");
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
