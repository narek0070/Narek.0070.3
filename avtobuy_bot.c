#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"
#include <sqlite3.h>

// Токен бота и имя вашего профиля Telegram
#define BOT_TOKEN "8948767241:AAGJ_TW1Y8XvZB0UKRDnlEZNw6xaZtlgl_o"
#define API_URL "https://api.telegram.org/bot" BOT_TOKEN "/"
#define ADMIN_TG_USERNAME "Narek0070"

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

// Отправка обычного сообщения
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

// Меню покупки пакетов с кнопками-ссылками на @Narek0070
void send_buy_menu(long long chat_id) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "chat_id", chat_id);
    cJSON_AddStringToObject(msg, "text", 
        "💳 *Выберите пакет поисков:*\n\n"
        "Нажмите на кнопку нужного пакета, чтобы перейди в диалог с администратором (@Narek0070). "
        "После перевода администратор подствердит платеж и поиски добавятся на ваш баланс.");
    cJSON_AddStringToObject(msg, "parse_mode", "Markdown");

    cJSON *reply_markup = cJSON_CreateObject();
    cJSON *inline_keyboard = cJSON_CreateArray();

    const char *labels[] = {
        "➕ 5 поисков — 200 AMD",
        "🔟 10 поисков — 300 AMD",
        "2️⃣5️⃣ 25 поисков — 500 AMD",
        "5️⃣0️⃣ 50 поисков — 900 AMD",
        "💯 100 поисков — 1500 AMD"
    };

    const char *package_names[] = {
        "5_searches_200AMD",
        "10_searches_300AMD",
        "25_searches_500AMD",
        "50_searches_900AMD",
        "100_searches_1500AMD"
    };

    for (int i = 0; i < 5; i++) {
        cJSON *row = cJSON_CreateArray();
        cJSON *button = cJSON_CreateObject();
        cJSON_AddStringToObject(button, "text", labels[i]);

        char url[512];
        // Передает выбранный пакет и chat_id в личные сообщения администратору
        snprintf(url, sizeof(url), 
                 "https://t.me/%s?text=Здравствуйте!%%20Хочу%%20купить%%20пакет:%%20%s.%%20Мой%%20ID:%%20%lld", 
                 ADMIN_TG_USERNAME, package_names[i], chat_id);
        
        cJSON_AddStringToObject(button, "url", url);

        cJSON_AddItemToArray(row, button);
        cJSON_AddItemToArray(inline_keyboard, row);
    }

    cJSON_AddItemToObject(reply_markup, "inline_keyboard", inline_keyboard);
    cJSON_AddItemToObject(msg, "reply_markup", reply_markup);

    char *json_out = cJSON_PrintUnformatted(msg);
    http_post("sendMessage", json_out);

    free(json_out);
    cJSON_Delete(msg);
}

// Инициализация базы данных пользователей
void init_database(sqlite3 **db) {
    sqlite3_open("cars_armenia.db", db);
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS users ("
        "chat_id INTEGER PRIMARY KEY, searches_left INT);"
    ;
    sqlite3_exec(*db, sql, 0, 0, 0);
}

// Получить баланс поисков (при первом входе дает 10 бесплатных)
int get_user_searches(sqlite3 *db, long long chat_id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "SELECT searches_left FROM users WHERE chat_id = %lld;", chat_id);
    sqlite3_stmt *stmt;
    int searches = -1;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            searches = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (searches == -1) {
        snprintf(sql, sizeof(sql), "INSERT INTO users (chat_id, searches_left) VALUES (%lld, 10);", chat_id);
        sqlite3_exec(db, sql, 0, 0, 0);
        searches = 10;
    }
    return searches;
}

void decrement_search(sqlite3 *db, long long chat_id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "UPDATE users SET searches_left = searches_left - 1 WHERE chat_id = %lld;", chat_id);
    sqlite3_exec(db, sql, 0, 0, 0);
}

void add_searches(sqlite3 *db, long long chat_id, int count) {
    char sql[128];
    snprintf(sql, sizeof(sql), "UPDATE users SET searches_left = searches_left + %d WHERE chat_id = %lld;", count, chat_id);
    sqlite3_exec(db, sql, 0, 0, 0);
}

// Генерация ссылки на List.am с готовым фильтром по введенной строке
void generate_list_am_filter_url(const char *query, char *url_out, size_t max_len) {
    char encoded[256];
    int j = 0;
    for (int i = 0; query[i] != '\0' && j < 240; i++) {
        if (query[i] == ' ') {
            encoded[j++] = '+';
        } else {
            encoded[j++] = query[i];
        }
    }
    encoded[j] = '\0';
    snprintf(url_out, max_len, "https://www.list.am/ru/q?q=%s", encoded);
}

// Главный цикл работы бота
void run_bot(sqlite3 *db) {
    long long last_update_id = 0;
    printf("🤖 Бот CarBuy AM с оплатой через @%s запущен...\n", ADMIN_TG_USERNAME);

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
                            int left = get_user_searches(db, chat_id);
                            char welcome[512];
                            snprintf(welcome, sizeof(welcome), 
                                     "🚗 Добро пожаловать в *CarBuy AM*!\n\n"
                                     "🔍 Доступно бесплатных поисков: *%d*\n\n"
                                     "Напишите марку, модель или год автомобиля (например: `BMW M5` или `Toyota RAV4 2023`), и бот мгновенно сформирует ссылку на **List.am** с примененным фильтром!", left);
                            send_text_message(chat_id, welcome);
                        }
                        else if (strcmp(text, "/buy") == 0) {
                            send_buy_menu(chat_id);
                        }
                        // АДМИН-КОМАНДА ОДОБРЕНИЯ: /add <chat_id> <количество>
                        else if (strncmp(text, "/add ", 5) == 0) {
                            long long target_chat_id;
                            int count;
                            if (sscanf(text + 5, "%lld %d", &target_chat_id, &count) == 2) {
                                add_searches(db, target_chat_id, count);
                                int new_balance = get_user_searches(db, target_chat_id);
                                
                                char admin_confirm[128];
                                snprintf(admin_confirm, sizeof(admin_confirm), "✅ Платеж одобрен! Начислено %d поисков пользователю %lld.", count, target_chat_id);
                                send_text_message(chat_id, admin_confirm);

                                char user_notify[256];
                                snprintf(user_notify, sizeof(user_notify), 
                                         "🎉 Ваш платеж одобрен! Начислено +%d поисков.\n"
                                         "Текущий баланс: *%d* поисков.", count, new_balance);
                                send_text_message(target_chat_id, user_notify);
                            } else {
                                send_text_message(chat_id, "❌ Использование: `/add CHAT_ID КОЛИЧЕСТВО`");
                            }
                        }
                        // АДМИН-КОМАНДА ОТКЛОНЕНИЯ: /reject <chat_id>
                        else if (strncmp(text, "/reject ", 8) == 0) {
                            long long target_chat_id;
                            if (sscanf(text + 8, "%lld", &target_chat_id) == 1) {
                                char admin_confirm[128];
                                snprintf(admin_confirm, sizeof(admin_confirm), "🛑 Платеж пользователя %lld отклонен.", target_chat_id);
                                send_text_message(chat_id, admin_confirm);

                                send_text_message(target_chat_id, "❌ **Платеж отклонен.**");
                            } else {
                                send_text_message(chat_id, "❌ Использование: `/reject CHAT_ID`");
                            }
                        }
                        else {
                            // Обычный поисковый запрос от пользователя
                            int left = get_user_searches(db, chat_id);
                            if (left <= 0) {
                                send_text_message(chat_id, "❌ У вас закончились бесплатные попытки поиска!\n\n💳 Отправьте /buy для покупки новых попыток.");
                            } else {
                                decrement_search(db, chat_id);
                                int new_left = get_user_searches(db, chat_id);

                                char filter_url[512];
                                generate_list_am_filter_url(text, filter_url, sizeof(filter_url));

                                char report[1024];
                                snprintf(report, sizeof(report), 
                                         "🇦🇲 *List.am — Поисковый фильтр готов*\n\n"
                                         "🚘 *Ваш запрос:* `%s`\n\n"
                                         "🔗 [Перейти к результатам на List.am](%s)\n\n"
                                         "🔍 Осталось попыток: *%d*\n"
                                         "💳 Пополнить баланс: /buy", 
                                         text, filter_url, new_left);

                                send_text_message(chat_id, report);
                            }
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
