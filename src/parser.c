/**
 * parser.c - FTP listing parser (Unix and Windows formats)
 */

#include "cftpfs.h"

static const char *skip_spaces(const char *str) {
    while (*str && isspace((unsigned char)*str)) str++;
    return str;
}

static int parse_month(const char *month_str) {
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(month_str, months[i], 3) == 0) {
            return i;
        }
    }
    return -1;
}

int parse_unix_listing(const char *line, ftp_item_t *item) {
    // Unix format: drwxr-xr-x 2 user group 4096 Jan 1 12:00 name
    // Or: drwxr-xr-x 2 user group 4096 Jan 1 2023 name
    
    if (strlen(line) < 10) return -1;
    
    // File type
    switch (line[0]) {
        case 'd':
            item->type = FTP_TYPE_DIR;
            item->mode = S_IFDIR | 0755;
            break;
        case '-':
            item->type = FTP_TYPE_FILE;
            item->mode = S_IFREG | 0644;
            break;
        case 'l':
            item->type = FTP_TYPE_LINK;
            item->mode = S_IFLNK | 0777;
            break;
        default:
            return -1;
    }
    
    // Skip permissions and links
    const char *p = line + 1;
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_spaces(p);
    
    // Skip link count
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_spaces(p);
    
    // Skip user
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_spaces(p);
    
    // Skip group
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_spaces(p);
    
    // Size
    item->size = 0;
    while (*p && isdigit((unsigned char)*p)) {
        item->size = item->size * 10 + (*p - '0');
        p++;
    }
    p = skip_spaces(p);
    
    // Date: Month
    char month_str[4] = {0};
    if (strlen(p) < 3) return -1;
    strncpy(month_str, p, 3);
    int month = parse_month(month_str);
    if (month < 0) return -1;
    p += 3;
    p = skip_spaces(p);
    
    // Day
    int day = 0;
    while (*p && isdigit((unsigned char)*p)) {
        day = day * 10 + (*p - '0');
        p++;
    }
    p = skip_spaces(p);
    
    // Time or Year
    int year = 0;
    int hour = 0;
    int min = 0;
    
    if (strchr(p, ':')) {
        // Time format: 12:00
        while (*p && isdigit((unsigned char)*p)) {
            hour = hour * 10 + (*p - '0');
            p++;
        }
        if (*p == ':') p++;
        while (*p && isdigit((unsigned char)*p)) {
            min = min * 10 + (*p - '0');
            p++;
        }
        // Use current year
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        year = tm_now->tm_year + 1900;
    } else {
        // Year format: 2023
        while (*p && isdigit((unsigned char)*p)) {
            year = year * 10 + (*p - '0');
            p++;
        }
        hour = 0;
        min = 0;
    }
    p = skip_spaces(p);
    
    // Filename
    if (*p == '\0') return -1;
    
    // If link, only take the part before " -> "
    char *arrow = strstr(p, " -> ");
    if (arrow) {
        *arrow = '\0';
    }
    
    strncpy(item->name, p, MAX_PATH_LEN - 1);
    item->name[MAX_PATH_LEN - 1] = '\0';
    
    // Build timestamp
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = 0;
    item->mtime = mktime(&tm);
    
    return 0;
}

int parse_windows_listing(const char *line, ftp_item_t *item) {
    // Windows format: 01-01-24  12:00PM              <DIR>          name
    // Or: 01-01-24  12:00PM                1234         file.txt
    
    if (strlen(line) < 20) return -1;
    
    // Parse date: MM-DD-YY or MM-DD-YYYY
    int month, day, year;
    if (sscanf(line, "%2d-%2d-%2d", &month, &day, &year) != 3) {
        return -1;
    }
    
    // Adjust year
    if (year < 50) {
        year += 2000;
    } else if (year < 100) {
        year += 1900;
    }
    
    const char *p = line + 8;  // Skip date
    p = skip_spaces(p);
    
    // Parse time
    int hour, min;
    char ampm[3] = {0};
    if (sscanf(p, "%2d:%2d%2s", &hour, &min, ampm) >= 2) {
        if (strcasecmp(ampm, "PM") == 0 && hour != 12) {
            hour += 12;
        } else if (strcasecmp(ampm, "AM") == 0 && hour == 12) {
            hour = 0;
        }
    }
    
    // Advance past time
    while (*p && !isspace((unsigned char)*p)) p++;
    p = skip_spaces(p);
    
    // Check if directory or file
    if (strncasecmp(p, "<DIR>", 5) == 0) {
        item->type = FTP_TYPE_DIR;
        item->mode = S_IFDIR | 0755;
        item->size = 0;
        p += 5;
    } else {
        item->type = FTP_TYPE_FILE;
        item->mode = S_IFREG | 0644;
        
        // Parse size
        item->size = 0;
        while (*p && isdigit((unsigned char)*p)) {
            item->size = item->size * 10 + (*p - '0');
            p++;
        }
    }
    
    p = skip_spaces(p);
    
    // Filename
    if (*p == '\0') return -1;
    
    strncpy(item->name, p, MAX_PATH_LEN - 1);
    item->name[MAX_PATH_LEN - 1] = '\0';
    
    // Strip trailing spaces
    int len = strlen(item->name);
    while (len > 0 && isspace((unsigned char)item->name[len - 1])) {
        item->name[--len] = '\0';
    }
    
    // Build timestamp
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = 0;
    item->mtime = mktime(&tm);
    
    return 0;
}

int parse_ftp_listing(const char *line, ftp_item_t *item) {
    if (!line || !item) return -1;
    
    memset(item, 0, sizeof(ftp_item_t));
    
    // Ignore empty lines
    const char *p = skip_spaces(line);
    if (*p == '\0') return -1;
    
    // Detect format by first character
    if (*p == 'd' || *p == '-' || *p == 'l') {
        // Formato Unix
        return parse_unix_listing(p, item);
    } else if (isdigit((unsigned char)*p)) {
        // Windows format (starts with date)
        return parse_windows_listing(p, item);
    }
    
    return -1;
}