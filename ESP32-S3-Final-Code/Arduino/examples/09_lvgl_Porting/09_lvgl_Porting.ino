#include <Arduino.h>
#include <esp_display_panel.hpp>
#include "waveshare_sd_card.h"
//#include <WiFi.h>
#include <lvgl.h>
#include "lvgl_v8_port.h"
esp_expander::CH422G *expander = NULL;
#include "esp_task_wdt.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

//const char* ssid = "realme GT 2";
//const char* password = "12345678";
//
//WiFiServer server(80);

// ---------------------------------------------------------------------------------------------------

lv_obj_t *area_label, *concentration_label, *chart, *slider1, *slider2, *label1, *label2, *file_list;
lv_chart_series_t *series;
static int lowerLimit = 2100, upperLimit = 2400;
const int arraySize = 7500;
float array1[arraySize], array2[arraySize];
String selectedFile;
float m_value = 0.0, c_value = 0.0;


int tempLowerLimit = 0, tempUpperLimit = 100;

void loadCSV(fs::FS &fs, const char *path) {
    String fullPath = String(path);  // Remove the redundant "/"
    // Serial.printf("Opening file: %s\n", fullPath.c_str());

    File file = fs.open(fullPath.c_str());  // Direct path access
    if (!file || file.isDirectory()) {
        // Serial.println("Failed to open file for reading or file is a directory.");
        return;
    }

    int index = 0;
    while (file.available() && index < arraySize) {
        String line = file.readStringUntil('\n');
        line.trim();

        int separatorIndex = line.indexOf(',');
        if (separatorIndex == -1) {
            separatorIndex = line.indexOf('\t');
        }

        if (separatorIndex != -1) {
            String xStr = line.substring(0, separatorIndex);
            String yStr = line.substring(separatorIndex + 1);

            float x = xStr.toFloat();
            float y = yStr.toFloat();

            array1[index] = x;
            array2[index] = y;


            index++;
        }
    }

    // Serial.printf("Total data points loaded: %d\n", index);
    file.close();
}



void update_area_label() {
    float area = computeAreaUnderCurve();
    float concentration = computeConcentration(area, m_value, c_value);

    // Serial.printf("Computed Area: %.2f\n", area);
    // Serial.printf("Computed Concentration: %.2f\n", concentration);

    static char areaText[50];
    snprintf(areaText, sizeof(areaText), "Area: %.2f", area);
    lv_label_set_text(area_label, areaText);

    static char concentrationText[50];
    snprintf(concentrationText, sizeof(concentrationText), "Concentration: %.2f mM", concentration);
    lv_label_set_text(concentration_label, concentrationText);

    lv_refr_now(NULL);
}

void slider_event_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider);

    if (slider == slider1) {
        tempLowerLimit = value;
        if (tempLowerLimit >= tempUpperLimit) {
            tempLowerLimit = tempUpperLimit - 1;
            if(tempLowerLimit < 0) tempLowerLimit = 0;  // Clamp to min
            lv_slider_set_value(slider1, tempLowerLimit, LV_ANIM_OFF);
        }
        lv_label_set_text_fmt(label1, "Wavenumber 1: %d", tempLowerLimit);
    }
    else if (slider == slider2) {
        tempUpperLimit = value;
        if (tempUpperLimit <= tempLowerLimit) {
            tempUpperLimit = tempLowerLimit + 1;
            if(tempUpperLimit >= arraySize) tempUpperLimit = arraySize - 1;  // Clamp to max
            lv_slider_set_value(slider2, tempUpperLimit, LV_ANIM_OFF);
        }
        lv_label_set_text_fmt(label2, "Wavenumber 2: %d", tempUpperLimit);
    }

    if (tempLowerLimit < tempUpperLimit) {
        lowerLimit = tempLowerLimit;
        upperLimit = tempUpperLimit;
        update_area_label();
        update_chart();
    }
}


//void update_chart() {
//    if (!chart || !series) {
//        // Serial.println("[ERROR] Chart or series is NULL");
//        return;
//    }
//
//    lowerLimit = 0;
//    upperLimit = arraySize - 1;
//
//    int num_points = upperLimit - lowerLimit;
//    if (num_points <= 0) {
//        // Serial.println("[WARN] No data points to plot.");
//        return;
//    }
//
//    num_points = min(num_points, 100);  // prevent overflow
//
//    static lv_coord_t data_array[100];  // persistent memory
//
//    for (int i = 0; i < num_points; i++) {
//        int index = lowerLimit + i;
//        data_array[i] = (index < arraySize) ? array2[index] : 0;
//    }
//
//    lv_chart_set_point_count(chart, num_points);
//    lv_chart_set_ext_y_array(chart, series, data_array);
//    lv_chart_refresh(chart);  // tell LVGL to redraw it
//
//    // Serial.println("[INFO] Chart updated.");
//}
void update_chart() {
    if (!chart || !series) {
        // Serial.println("[ERROR] Chart or series is NULL");
        return;
    }

    // Filter data points in range 2100 <= x <= 2400 and count them
    int count = 0;
    for (int i = 0; i < arraySize; i++) {
        if (array1[i] >= 2100 && array1[i] <= 2400) {
            count++;
        }
    }
    if (count == 0) {
        // Serial.println("[WARN] No data points in the range 2100 to 2400.");
        return;
    }

    // Allocate lv_coord_t data array once or resize
    static lv_coord_t* data_array = nullptr;
    static int allocated_size = 0;
    if (!data_array || allocated_size < count) {
        if (data_array) free(data_array);
        data_array = (lv_coord_t*)heap_caps_malloc(sizeof(lv_coord_t) * count, MALLOC_CAP_INTERNAL);
        if (!data_array) {
            // Serial.println("[ERROR] Memory allocation failed.");
            return;
        }
        allocated_size = count;
    }

    // Find min and max float y-values in the range
    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < arraySize; i++) {
        if (array1[i] >= 2100 && array1[i] <= 2400) {
            if (array2[i] < minY) minY = array2[i];
            if (array2[i] > maxY) maxY = array2[i];
        }
    }
    if (minY == maxY) maxY = minY + 1.0f; // prevent div by zero

    // Scale float y to integer range based on minY, maxY for smoothness
    // Let's pick range 0..1000 for good resolution
    const int SCALE_MAX = 1000;

    int j = 0;
    for (int i = 0; i < arraySize; i++) {
        if (array1[i] >= 2100 && array1[i] <= 2400) {
            float val = array2[i];
            // Scale val from minY..maxY to 0..SCALE_MAX
            lv_coord_t int_val = (lv_coord_t)((val - minY) * SCALE_MAX / (maxY - minY));
            data_array[j++] = int_val;
        }
    }

    lv_chart_set_point_count(chart, count);
    lv_chart_set_ext_y_array(chart, series, data_array);

    // Set Y axis range to match scaled range
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, SCALE_MAX);

    lv_chart_refresh(chart);

    // Serial.printf("[INFO] Chart updated with %d points, Y range [%.2f .. %.2f], scaled to [0..%d].\n", count, minY, maxY, SCALE_MAX);
}


void file_select_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
//    #################################################################
//    selectedFile = (const char*)lv_list_get_btn_text(file_list, btn);
//    // Serial.printf("Selected file: %s\n", selectedFile.c_str());
//    #################################################################
const char *path = (const char *)lv_event_get_user_data(e);
selectedFile = String(path);
free((void *)path);  // Free the allocated memory

    // Serial.printf("Selected file: %s\n", selectedFile.c_str());

    lv_obj_clean(lv_scr_act());

    createParameterInputPage();
}

lv_obj_t *m_input, *c_input, *keyboard;

//void createParameterInputPage() {
//    lv_obj_clean(lv_scr_act());
//
//    lv_obj_t *title = lv_label_create(lv_scr_act());
//    lv_label_set_text(title, "Enter values for the Equation: y = mx+c:");
//    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
//
//    m_input = lv_textarea_create(lv_scr_act());
//    lv_textarea_set_placeholder_text(m_input, "Enter m value");
//    lv_obj_set_size(m_input, 250, 40);
//    lv_obj_align(m_input, LV_ALIGN_CENTER, -150, -50);
//    lv_obj_add_event_cb(m_input, keyboard_event_cb, LV_EVENT_FOCUSED, NULL);
//
//    c_input = lv_textarea_create(lv_scr_act());
//    lv_textarea_set_placeholder_text(c_input, "Enter c value");
//    lv_obj_set_size(c_input, 250, 40);
//    lv_obj_align(c_input, LV_ALIGN_CENTER, 150, -50);
//    lv_obj_add_event_cb(c_input, keyboard_event_cb, LV_EVENT_FOCUSED, NULL);
//
//    keyboard = lv_keyboard_create(lv_scr_act());
//    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
//    lv_obj_set_size(keyboard, 350, 150);
//    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
//    lv_keyboard_set_textarea(keyboard, m_input);
//
//    lv_obj_t *next_btn = lv_btn_create(lv_scr_act());
//    lv_obj_set_size(next_btn, 150, 50);
//    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 300, -10);
//    lv_obj_add_event_cb(next_btn, next_button_cb, LV_EVENT_CLICKED, NULL);
//
//    lv_obj_t *btn_label = lv_label_create(next_btn);
//    lv_label_set_text(btn_label, "Next");
//    lv_obj_center(btn_label);
//
//    static lv_style_t button_style;
//    lv_style_init(&button_style);
//    lv_style_set_radius(&button_style, 12);
//    lv_style_set_border_width(&button_style, 2);
//    lv_style_set_border_color(&button_style, lv_palette_main(LV_PALETTE_BLUE));
//    lv_style_set_bg_color(&button_style, lv_palette_main(LV_PALETTE_CYAN));
//    lv_style_set_text_color(&button_style, lv_color_white());
//
//    lv_textarea_set_text(m_input, "329");
//lv_textarea_set_text(c_input, "22960");
//
//    lv_obj_add_style(next_btn, &button_style, 0);
//}
//
//void keyboard_event_cb(lv_event_t *e) {
//    lv_obj_t *ta = lv_event_get_target(e);
//    lv_keyboard_set_textarea(keyboard, ta);
//}

void createParameterInputPage() {
    lv_obj_clean(lv_scr_act());

    // Title label
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Enter values for the Equation: y = mx + c");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Text area for 'm'
    m_input = lv_textarea_create(lv_scr_act());
    lv_textarea_set_placeholder_text(m_input, "Enter m value");
    lv_obj_set_size(m_input, 250, 40);
    lv_obj_align(m_input, LV_ALIGN_CENTER, -130, -50);
    lv_obj_add_event_cb(m_input, keyboard_event_cb, LV_EVENT_CLICKED, NULL); // Use CLICKED not FOCUSED

    // Text area for 'c'
    c_input = lv_textarea_create(lv_scr_act());
    lv_textarea_set_placeholder_text(c_input, "Enter c value");
    lv_obj_set_size(c_input, 250, 40);
    lv_obj_align(c_input, LV_ALIGN_CENTER, 130, -50);
    lv_obj_add_event_cb(c_input, keyboard_event_cb, LV_EVENT_CLICKED, NULL);

    // On-screen keyboard
    keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(keyboard, 350, 150);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(keyboard, m_input); // Initially bind to m_input

    // Next button
    lv_obj_t *next_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(next_btn, 150, 50);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -10);  // Better positioning
    lv_obj_add_event_cb(next_btn, next_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(next_btn);
    lv_label_set_text(btn_label, "Next");
    lv_obj_center(btn_label);

    // Styling for the button
    static lv_style_t button_style;
    lv_style_init(&button_style);
    lv_style_set_radius(&button_style, 12);
    lv_style_set_border_width(&button_style, 2);
    lv_style_set_border_color(&button_style, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_color(&button_style, lv_palette_main(LV_PALETTE_CYAN));
    lv_style_set_text_color(&button_style, lv_color_white());
    lv_obj_add_style(next_btn, &button_style, 0);

    // Optional: Pre-fill inputs for testing
//    lv_textarea_set_text(m_input, "329");
//    lv_textarea_set_text(c_input, "22960");
}


void keyboard_event_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (lv_keyboard_get_textarea(keyboard) != ta && ta != NULL) {
        lv_keyboard_set_textarea(keyboard, ta);
    }
}


void next_button_cb(lv_event_t *e) {
    m_value = atof(lv_textarea_get_text(m_input));
    c_value = atof(lv_textarea_get_text(c_input));

    Serial.printf("Entered m: %.2f, c: %.2f\n", m_value, c_value);

    // Ensure selectedFile stores the full path, not just the filename
    String fullFilePath = selectedFile;

    // Serial.printf("Full file path: %s\n", fullFilePath.c_str());

    // Load the CSV file using the full file path
    loadCSV(SD, fullFilePath.c_str());
    //loadCSV(SD, "/Cu-2-8 mM/Cu-2mM.csv");
    
    createUI();
}




void listCSVFilesRecursively(fs::FS &fs, const char *dirname, std::vector<String> &csvFiles, std::vector<String> &csvFileNames) {
    // Serial.printf("Listing CSV files in directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) {
        // Serial.println("Failed to open directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            listCSVFilesRecursively(fs, file.path(), csvFiles, csvFileNames);
        } else {
            String filePath = file.path();
            if (filePath.endsWith(".csv")) {
                // Serial.printf("Found CSV file: %s\n", filePath.c_str());
                csvFiles.push_back(filePath);
                csvFileNames.push_back(file.name());
            }
        }
        file = root.openNextFile();
    }
}


struct PageData {
    int *current_page;
    int total_pages;
};

void prev_btn_event_handler(lv_event_t *e) {
    PageData *data = static_cast<PageData *>(lv_event_get_user_data(e));
    if (*(data->current_page) > 0) {
        (*(data->current_page))--;
        lv_obj_clean(lv_scr_act());
        createFileSelector();
    }
}

void next_btn_event_handler(lv_event_t *e) {
    PageData *data = static_cast<PageData *>(lv_event_get_user_data(e));
    if (*(data->current_page) < data->total_pages - 1) {
        (*(data->current_page))++;
        lv_obj_clean(lv_scr_act());
        createFileSelector();
    }
}

void createFileSelector() {
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Please select your CSV file");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00CFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    file_list = lv_list_create(lv_scr_act());
    lv_obj_set_size(file_list, 250, 300);
    lv_obj_align(file_list, LV_ALIGN_CENTER, 0, 60);
    lv_obj_clear_flag(file_list, LV_OBJ_FLAG_SCROLLABLE);

    static lv_style_t file_list_style;
    lv_style_init(&file_list_style);
    lv_style_set_radius(&file_list_style, 12);
    lv_style_set_border_width(&file_list_style, 2);
    lv_style_set_border_color(&file_list_style, lv_palette_main(LV_PALETTE_CYAN));
    lv_style_set_pad_all(&file_list_style, 10);
    lv_obj_add_style(file_list, &file_list_style, 0);

    std::vector<String> csvFiles;       // Full paths
    std::vector<String> csvFileNames;   // File names only
    listCSVFilesRecursively(SD, "/", csvFiles, csvFileNames);

    // Pagination
    static int current_page = 0;
    int files_per_page = 5;
    int total_pages = (csvFileNames.size() + files_per_page - 1) / files_per_page;

    int start_idx = current_page * files_per_page;
    int end_idx = std::min(start_idx + files_per_page, (int)csvFileNames.size());

    // File list buttons
    for (int i = start_idx; i < end_idx; i++) {
        String fileName = csvFileNames[i];  // e.g., Cu-2mM.csv
        String fullPath = csvFiles[i];      // e.g., /2/Cu-2mM.csv

        lv_obj_t *btn = lv_list_add_btn(file_list, NULL, fileName.c_str());

        // Pass full path as user data
        char *path_copy = strdup(fullPath.c_str());  // Must free this later in callback
        lv_obj_add_event_cb(btn, file_select_cb, LV_EVENT_CLICKED, path_copy);
    }

    // Navigation buttons
    lv_obj_t *prev_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(prev_btn, 50, 50);
    lv_obj_align(prev_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, "<");
    lv_obj_center(prev_label);

    lv_obj_t *next_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(next_btn, 50, 50);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, ">");
    lv_obj_center(next_label);

    lv_obj_t *page_label = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(page_label, "Page %d/%d", current_page + 1, total_pages);
    lv_obj_align(page_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    static PageData pageData = {&current_page, total_pages};
    lv_obj_add_event_cb(prev_btn, prev_btn_event_handler, LV_EVENT_CLICKED, &pageData);
    lv_obj_add_event_cb(next_btn, next_btn_event_handler, LV_EVENT_CLICKED, &pageData);

    if (current_page == 0) {
        lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
    }
    if (current_page == total_pages - 1) {
        lv_obj_add_state(next_btn, LV_STATE_DISABLED);
    }
}


float computeAreaUnderCurve() {
    float area = 0.0;
    Serial.print("The lowerlimit is ");
Serial.print(lowerLimit);
Serial.print(" and upperlimit is ");
Serial.println(upperLimit);
    for (int i = lowerLimit; i < upperLimit - 1; i++) {
      
        float x1 = array1[i];
        float x2 = array1[i + 1];
        float y1 = array2[i];
        float y2 = array2[i + 1];

        float dx = fabs(x2 - x1);  // Absolute value
        area += 0.5 * dx * (y1 + y2);
    }
    Serial.print("The area computed is");
    Serial.println(area);
    return area;
}




float computeConcentration(float area, float m, float c) {
    return (area-c)/m;
}

void update_button_cb(lv_event_t *e) {
    update_area_label();
}

void file_selector_button_cb(lv_event_t *e) {
    lv_obj_clean(lv_scr_act());
    createFileSelector();
}

void createUI() {
    // Serial.println("=== Printing first 100 array values ===");
for (int i = 0; i < 100; i++) {
    // Serial.print("array1[");
    // Serial.print(i);
    // Serial.print("] = ");
    // Serial.print(array1[i]);
    // Serial.print("  |  array2[");
    // Serial.print(i);
    // Serial.print("] = ");
    // Serial.println(array2[i]);
}
// Serial.println("=== Done printing ===");


    // Serial.println("Creating UI...");
    lv_obj_clean(lv_scr_act());

    // Create chart
    chart = lv_chart_create(lv_scr_act());
    lv_obj_set_size(chart, 600, 300);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 50);
    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Create slider 1
    slider1 = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider1, 300, 30);
    lv_slider_set_range(slider1, 0, arraySize - 1);
    lv_obj_align(slider1, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_add_event_cb(slider1, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label1 = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(label1, "Wavenumber 1: %d", lowerLimit);
    lv_obj_align_to(label1, slider1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // Create slider 2
    slider2 = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider2, 300, 30);
    lv_slider_set_range(slider2, 0, arraySize - 1);
    lv_obj_align(slider2, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_add_event_cb(slider2, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label2 = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(label2, "Wavenumber 2: %d", upperLimit);
    lv_obj_align_to(label2, slider2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // Create area label
    area_label = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(area_label, "Area: %.2f", computeAreaUnderCurve());
    lv_obj_align(area_label, LV_ALIGN_BOTTOM_LEFT, 20, -40);

    // Create concentration label (centered between the sliders and the plot)
// Create concentration label (centered between the sliders and the plot)
// Create concentration label (centered between the sliders and the plot)
//concentration_label = lv_label_create(lv_scr_act());
//lv_label_set_text_fmt(concentration_label, "Concentration: %.2f mM", computeConcentration(computeAreaUnderCurve(), m_value, c_value));

concentration_label = lv_label_create(lv_scr_act());

float area = computeAreaUnderCurve();
float concentration = computeConcentration(area, m_value, c_value);

// Defensive string formatting
char buf[64];
snprintf(buf, sizeof(buf), "Concentration: %.2f mM", concentration);
lv_label_set_text(concentration_label, buf);


// Create style for the concentration label
static lv_style_t concentration_style;
lv_style_init(&concentration_style);
lv_style_set_text_font(&concentration_style, &lv_font_montserrat_26);  // Larger font (replace if needed)
lv_style_set_text_color(&concentration_style, lv_palette_main(LV_PALETTE_RED));  // Use a striking color
lv_style_set_border_color(&concentration_style, lv_palette_darken(LV_PALETTE_BLUE, 3)); // Blue border
lv_style_set_border_width(&concentration_style, 2); // Slight border for emphasis
lv_style_set_bg_color(&concentration_style, lv_palette_lighten(LV_PALETTE_YELLOW, 2)); // Light yellow background
lv_style_set_pad_all(&concentration_style, 10);  // Padding for better spacing
lv_style_set_radius(&concentration_style, 8);  // Rounded corners

// Apply style to the concentration label (Pass 0 instead of LV_PART_MAIN)
lv_obj_add_style(concentration_label, &concentration_style, 0);

// Position the concentration label
lv_obj_align(concentration_label, LV_ALIGN_CENTER, 0, -120);


    // Create file selector button
    lv_obj_t *file_selector_button = lv_btn_create(lv_scr_act());
    lv_obj_align(file_selector_button, LV_ALIGN_BOTTOM_LEFT, 20, -100);
    lv_obj_add_event_cb(file_selector_button, file_selector_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *file_selector_label = lv_label_create(file_selector_button);
    lv_label_set_text(file_selector_label, "Select CSV File");
    lv_obj_center(file_selector_label);

    // Initially update the chart, area, and concentration labels
    update_chart();     // Initial chart update
    update_area_label();  // Initial area and concentration label update
}

// ---------------------------------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    // Serial.println("LVGL porting example");
//// Serial.println("Connecting to WiFi...");
//WiFi.begin(ssid, password);
//
//unsigned long startAttemptTime = millis();
//const unsigned long timeout = 10000; // 10 seconds
//
//while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
//    delay(500);
//    // Serial.print(".");
//}
//
//if (WiFi.status() == WL_CONNECTED) {
//    // Serial.println("\nWiFi connected!");
//    // Serial.print("IP Address: ");
//    // Serial.println(WiFi.localIP());
//} else {
//    // Serial.println("\n[ERROR] WiFi connection failed.");
//    // You can retry later or enter fallback mode
//}
//
//server.begin();


    // ######################## BOARD SETUP #############################

    // Serial.println("Initializing board");
    Board *board = new Board();
    
    // Serial.println("1");
    board->init();
    // Serial.println("2");

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
    // Serial.println("3");
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
    // Serial.println("4");
#endif
#endif

    // Serial.println("5");

    if (!board->begin()) {
        // Serial.println("Board begin failed, aborting.");
        while (true) delay(1000);
    }

    // Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    // ###################### IO EXPANDER SETUP ##########################
    // Serial.println("Initializing IO expander");

     //   auto expander_base = board->getExpander(); // returns esp_expander::Base*
    auto io_expander = board->getIO_Expander();
    if (!io_expander) {
        // Serial.println("Expander not initialized!");
        return;
    }
    
    auto base = io_expander->getBase();
    auto ch422g = static_cast<esp_expander::CH422G *>(base);
    
    if (!ch422g) {
        // Serial.println("Expander is not CH422G!");
        return;
    }

    // ###################### SD SETUP ##########################

    SPI.setHwCs(false);
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, -1);  // CS handled via expander

    // Serial.println("Mounting SD card...");
    ch422g->digitalWrite(SD_CS, LOW);  // Select card
    delay(10);  // let the card settle

    if (!SD.begin(-1)) {
        // Serial.println("Card Mount Failed");
        ch422g->digitalWrite(SD_CS, HIGH);
        delay(10); // Let bus settle

        return;
    }

    uint8_t cardType = SD.cardType();
if (cardType == CARD_NONE) {
    // Serial.println("No SD card attached");
    return;
}
// Serial.println("SD card detected");

    // Serial.println("Creating UI");
    lvgl_port_lock(-1);
    createFileSelector();  // Replace with your actual UI function
    lvgl_port_unlock();

}



//void loop() {
//  esp_task_wdt_reset();  // call inside the loop if needed
//   // lv_task_handler();  // Handling LVGL tasks
//    delay(10);  // Shorter delay for more responsive UI
//
//    WiFiClient client = server.available();
//    if (client) {
//        String currentLine = "";
//        String header = "";
//        
//        unsigned long currentTime = millis();
//        unsigned long previousTime = currentTime;
//        const long timeoutTime = 2000; // 2 seconds timeout
//        
//        while (client.connected() && (currentTime - previousTime <= timeoutTime)) {
//            currentTime = millis();
//            if (client.available()) {
//                char c = client.read();
//                header += c;
//                
//                if (c == '\n') {
//                    if (currentLine.length() == 0) {
//                        // End of HTTP header, send response
//                        client.println("HTTP/1.1 200 OK");
//                        client.println("Content-type:text/html");
//                        client.println("Connection: close");
//                        client.println();
//                        
//                        // Check if this is a POST request with form data
//                        if (header.indexOf("POST") >= 0) {
//                            // Wait for the POST data
//                            String postData = "";
//                            while (client.available()) {
//                                char c = client.read();
//                                postData += c;
//                            }
//                            
//                            // Parse POST data for slider values
//                            if (postData.indexOf("slider1=") >= 0) {
//                                int startPos = postData.indexOf("slider1=") + 8;
//                                int endPos = postData.indexOf("&", startPos);
//                                if (endPos < 0) endPos = postData.length();
//                                String valueStr = postData.substring(startPos, endPos);
//                                tempLowerLimit = valueStr.toInt();
//                                if (tempLowerLimit >= tempUpperLimit) {
//                                    tempLowerLimit = tempUpperLimit - 1;
//                                }
//                            }
//                            
//                            if (postData.indexOf("slider2=") >= 0) {
//                                int startPos = postData.indexOf("slider2=") + 8;
//                                int endPos = postData.indexOf("&", startPos);
//                                if (endPos < 0) endPos = postData.length();
//                                String valueStr = postData.substring(startPos, endPos);
//                                tempUpperLimit = valueStr.toInt();
//                                if (tempUpperLimit <= tempLowerLimit) {
//                                    tempUpperLimit = tempLowerLimit + 1;
//                                }
//                            }
//                            
//                            // Update limits if valid
//                            if (tempLowerLimit < tempUpperLimit) {
//                                lowerLimit = tempLowerLimit;
//                                upperLimit = tempUpperLimit;
//                                update_area_label();
//                                update_chart();
//                            }
//                        }
//                        
//                        // Calculate values for display
//                        float area = computeAreaUnderCurve();
//                        float concentration = computeConcentration(area, m_value, c_value);
//                        
//                        // Create JSON data for the chart
//                        String chartData = "[";
//                        int num_points = min(100, upperLimit - lowerLimit);
//                        if (num_points > 0) {
//                            for (int i = 0; i < num_points; i++) {
//                                int index = lowerLimit + i * (upperLimit - lowerLimit) / num_points;
//                                if (index < arraySize) {
//                                    if (i > 0) chartData += ",";
//                                    chartData += "{\"x\":" + String(array1[index]) + ",\"y\":" + String(array2[index]) + "}";
//                                }
//                            }
//                        }
//                        chartData += "]";
//                        
//                        // Start sending the HTML content
//                        client.println("<!DOCTYPE html><html>");
//                        client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
//                        client.println("<link rel=\"icon\" href=\"data:,\">");
//                        client.println("<title>Concentration Calculator</title>");
//                        client.println("<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>");
//                        client.println("<style>");
//                        client.println("@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600&display=swap');");
//                        client.println("html, body { font-family: 'Inter', sans-serif; margin: 0; padding: 0; background: #121212; color: #e0e0e0; display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; }");
//                        client.println(".container { text-align: center; width: 90%; max-width: 800px; background: #1e1e1e; padding: 20px; border-radius: 12px; box-shadow: 0px 4px 10px rgba(0,0,0,0.3); }");
//                        client.println("h1 { font-size: 24px; font-weight: 600; margin-bottom: 10px; }");
//                        client.println(".chart-container { width: 100%; height: 60vh; margin: 20px 0; }");
//                        client.println(".slider-container { display: flex; flex-direction: column; align-items: center; width: 100%; margin-top: 20px; }");
//                        client.println(".slider { width: 80%; max-width: 500px; height: 8px; background: linear-gradient(45deg, #3498db, #9b59b6); border-radius: 4px; outline: none; transition: 0.3s; margin: 10px 0; }");
//                        client.println(".slider:hover { transform: scale(1.05); }");
//                        client.println(".label { font-size: 18px; font-weight: 400; text-align: center; color: #ccc; margin-top: 5px; }");
//                        client.println(".btn { background: linear-gradient(45deg, #1abc9c, #16a085); color: white; padding: 12px 24px; border-radius: 8px; cursor: pointer; border: none; font-size: 16px; transition: all 0.3s; margin-top: 20px; display: block; margin-left: auto; margin-right: auto; }");
//                        client.println(".btn:hover { background: linear-gradient(45deg, #16a085, #1abc9c); transform: scale(1.05); }");
//                        client.println(".highlight { color: #f39c12; font-size: 24px; font-weight: bold; margin: 20px 0; padding: 10px; background: rgba(243, 156, 18, 0.1); border-radius: 8px; }");
//                        client.println(".loader { width: 50px; height: 50px; border: 6px solid #ddd; border-top: 6px solid #3498db; border-radius: 50%; animation: spin 1.5s linear infinite; margin: 20px auto; }");
//                        client.println("@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }");
//                        client.println(".controls { width: 100%; max-width: 800px; margin: 20px auto; padding: 15px; background: #2a2a2a; border-radius: 8px; }");
//                        client.println("form { margin: 0; }");
//                        client.println("</style>");
//                        client.println("</head>");
//                        client.println("<body>");
//                        client.println("<div class=\"container\">");
//                        client.println("<div class=\"loader\" id=\"loader\"></div>");
//                        client.println("<div class=\"content\" id=\"mainContent\" style=\"display: none;\">");
//                        client.println("<h1>Concentration Calculator</h1>");
//
//                        // Display the concentration value here
//                        client.println("<div id=\"concentrationLabel\" class=\"highlight\">Concentration: " + String(concentration, 2) + "</div>");
//                        client.println("<div id=\"areaLabel\" class=\"label\">Area: " + String(area, 2) + "</div>");
//
//                        client.println("<div class=\"chart-container\">");
//                        client.println("<canvas id=\"myChart\"></canvas>");
//                        client.println("</div>");
//
//                        // Send the sliders and other controls
//                        client.println("<div class=\"controls\">");
//                        client.println("<form id=\"sliderForm\" method=\"POST\">");
//                        client.println("<div class=\"slider-container\">");
//                        client.println("<input id=\"slider1\" name=\"slider1\" type=\"range\" min=\"0\" max=\"" + String(arraySize - 1) + "\" value=\"" + String(lowerLimit) + "\" class=\"slider\" onchange=\"updateLabels()\" oninput=\"updateLabels()\"/>");
//                        client.println("<div id=\"label1\" class=\"label\">Wavenumber 1: " + String(lowerLimit) + "</div>");
//                        client.println("</div>");
//
//                        client.println("<div class=\"slider-container\">");
//                        client.println("<input id=\"slider2\" name=\"slider2\" type=\"range\" min=\"0\" max=\"" + String(arraySize - 1) + "\" value=\"" + String(upperLimit) + "\" class=\"slider\" onchange=\"updateLabels()\" oninput=\"updateLabels()\"/>");
//                        client.println("<div id=\"label2\" class=\"label\">Wavenumber 2: " + String(upperLimit) + "</div>");
//                        client.println("</div>");
//                        
//                        client.println("<button type=\"submit\" class=\"btn\">Update Graph</button>");
//                        client.println("</form>");
//                        client.println("</div>");
//
//                        client.println("</div>");
//                        client.println("</div>");
//                        
//                        client.println("<script>");
//                        client.println("// Chart data from server");
//                        client.println("const chartData = " + chartData + ";");
//                        
//                        client.println("// Hide loader and show content");
//                        client.println("document.getElementById('loader').style.display = 'none';");
//                        client.println("document.getElementById('mainContent').style.display = 'block';");
//                        
//                        client.println("// Update slider labels");
//                        client.println("function updateLabels() {");
//                        client.println("  const slider1 = document.getElementById('slider1');");
//                        client.println("  const slider2 = document.getElementById('slider2');");
//                        client.println("  document.getElementById('label1').textContent = 'Wavenumber 1: ' + slider1.value;");
//                        client.println("  document.getElementById('label2').textContent = 'Wavenumber 2: ' + slider2.value;");
//                        client.println("  // Ensure slider1 < slider2");
//                        client.println("  if(parseInt(slider1.value) >= parseInt(slider2.value)) {");
//                        client.println("    if(parseInt(slider1.value) >= parseInt(slider2.max) - 1) {");
//                        client.println("      slider1.value = parseInt(slider2.max) - 2;");
//                        client.println("    } else {");
//                        client.println("      slider2.value = parseInt(slider1.value) + 1;");
//                        client.println("    }");
//                        client.println("    document.getElementById('label1').textContent = 'Wavenumber 1: ' + slider1.value;");
//                        client.println("    document.getElementById('label2').textContent = 'Wavenumber 2: ' + slider2.value;");
//                        client.println("  }");
//                        client.println("}");
//                        
//                        client.println("// Create chart");
//                        client.println("const ctx = document.getElementById('myChart').getContext('2d');");
//                        client.println("const myChart = new Chart(ctx, {");
//                        client.println("  type: 'line',");
//                        client.println("  data: {");
//                        client.println("    datasets: [{");
//                        client.println("      label: 'Spectral Data',");
//                        client.println("      data: chartData,");
//                        client.println("      backgroundColor: 'rgba(52, 152, 219, 0.2)',");
//                        client.println("      borderColor: 'rgba(52, 152, 219, 1)',");
//                        client.println("      borderWidth: 2,");
//                        client.println("      pointRadius: 0,");
//                        client.println("      fill: true,");
//                        client.println("      tension: 0.1");
//                        client.println("    }]");
//                        client.println("  },");
//                        client.println("  options: {");
//                        client.println("    responsive: true,");
//                        client.println("    maintainAspectRatio: false,");
//                        client.println("    scales: {");
//                        client.println("      x: {");
//                        client.println("        type: 'linear',");
//                        client.println("        position: 'bottom',");
//                        client.println("        title: {");
//                        client.println("          display: true,");
//                        client.println("          text: 'Wavenumber',");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        },");
//                        client.println("        grid: {");
//                        client.println("          color: 'rgba(255, 255, 255, 0.1)'");
//                        client.println("        },");
//                        client.println("        ticks: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      },");
//                        client.println("      y: {");
//                        client.println("        title: {");
//                        client.println("          display: true,");
//                        client.println("          text: 'Intensity',");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        },");
//                        client.println("        grid: {");
//                        client.println("          color: 'rgba(255, 255, 255, 0.1)'");
//                        client.println("        },");
//                        client.println("        ticks: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      }");
//                        client.println("    },");
//                        client.println("    plugins: {");
//                        client.println("      legend: {");
//                        client.println("        labels: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      },");
//                        client.println("      tooltip: {");
//                        client.println("        mode: 'index',");
//                        client.println("        intersect: false,");
//                        client.println("        backgroundColor: 'rgba(0, 0, 0, 0.8)'");
//                        client.println("      }");
//                        client.println("    }");
//                        client.println("  }");
//                        client.println("});");
//                        
//                        client.println("// Add shaded area between sliders");
//                        client.println("const addAreaPlugin = {");
//                        client.println("  id: 'customCanvasBackgroundColor',");
//                        client.println("  beforeDraw: (chart) => {");
//                        client.println("    const ctx = chart.canvas.getContext('2d');");
//                        client.println("    const slider1 = parseInt(document.getElementById('slider1').value);");
//                        client.println("    const slider2 = parseInt(document.getElementById('slider2').value);");
//                        client.println("    const xScale = chart.scales.x;");
//                        client.println("    const yScale = chart.scales.y;");
//                        client.println("    ");
//                        client.println("    // Find x positions on chart");
//                        client.println("    let xPos1 = null, xPos2 = null;");
//                        client.println("    for(let i = 0; i < chartData.length; i++) {");
//                        client.println("      const x = chartData[i].x;");
//                        client.println("      if (xPos1 === null && x >= slider1) {");
//                        client.println("        xPos1 = xScale.getPixelForValue(x);");
//                        client.println("      }");
//                        client.println("      if (xPos2 === null && x >= slider2) {");
//                        client.println("        xPos2 = xScale.getPixelForValue(x);");
//                        client.println("        break;");
//                        client.println("      }");
//                        client.println("    }");
//                        client.println("    ");
//                        client.println("    if (xPos1 !== null && xPos2 !== null) {");
//                        client.println("      ctx.save();");
//                        client.println("      ctx.fillStyle = 'rgba(243, 156, 18, 0.2)';");
//                        client.println("      ctx.fillRect(xPos1, yScale.top, xPos2 - xPos1, yScale.bottom - yScale.top);");
//                        client.println("      ctx.restore();");
//                        client.println("    }");
//                        client.println("  }");
//                        client.println("};");
//                        client.println("Chart.register(addAreaPlugin);");
//                        
//                        client.println("</script>");
//                        client.println("</body>");
//                        client.println("</html>");

//                        client.flush();
//                        break;
//                    } else {
//                        currentLine = "";
//                    }
//                } else if (c != '\r') {
//                    currentLine += c;
//                }
//            }
//        }
//        client.stop();
//    }
//}
//
//
//// Helper function to get the slider value from the HTTP request
//int getSliderValue(WiFiClient &client, String sliderId, int currentValue) {
//    String valueStr = "";
//    while (client.available()) {
//        char c = client.read();
//        if (c == '&' || c == ' ' || c == '=') continue;  // Ignore unnecessary characters
//        valueStr += c;
//        if (valueStr.indexOf(sliderId) != -1) {
//            int startPos = valueStr.indexOf('=') + 1;
//            if (startPos != -1) {
//                valueStr = valueStr.substring(startPos);
//                currentValue = valueStr.toInt();
//                break;
//            }
//        }
//    }
//    return currentValue;
//}

void loop () {}

//void loop() {
//    lv_task_handler();
//    float minY = 1e9f, maxY = -1e9f;
//    for (int i = 0; i < arraySize; i++) {
//        if (array1[i] >= 2100 && array1[i] <= 2400) {
//            if (array2[i] < minY) minY = array2[i];
//            if (array2[i] > maxY) maxY = array2[i];
//        }
//    }
//    if (minY == maxY) maxY = minY + 1.0f;
//    delay(70);
//    // Serial.println("Test 1");
//    // Handle incoming WiFi client requests (non-blocking)
//    
//    WiFiClient client = server.available();
//    if (client) {
//      
//        String currentLine = "";
//        String header = "";
//        unsigned long startTime = millis();
//        const unsigned long timeout = 200; // Short timeout (ms)
//        // Serial.println("Test 2");
//        // Non-blocking: process available data, but don't wait
//        while (client.connected() && (millis() - startTime < timeout)) {
//              lv_task_handler();  // ✅ keep display alive
//              yield();
//            if (client.available()) {
//                char c = client.read();
//                header += c;
//
//                // Detect end of HTTP header
//                if (c == '\n') {
//                    if (currentLine.length() == 0) {
//                        // Send HTTP response
//                        client.println("HTTP/1.1 200 OK");
//                        client.println("Content-type:text/html");
//                        client.println("Connection: close");
//                        client.println();
//
//                        // (Optional) Handle POST data and update your UI or variables here
//// Check if this is a POST request with form data
//                        if (header.indexOf("POST") >= 0) {
//                            // Wait for the POST data
//                            String postData = "";
//                            while (client.available()) {
//                                char c = client.read();
//                                postData += c;
//                            }
//                            
//                            // Parse POST data for slider values
//                            if (postData.indexOf("slider1=") >= 0) {
//                                int startPos = postData.indexOf("slider1=") + 8;
//                                int endPos = postData.indexOf("&", startPos);
//                                if (endPos < 0) endPos = postData.length();
//                                String valueStr = postData.substring(startPos, endPos);
//                                tempLowerLimit = valueStr.toInt();
//                                if (tempLowerLimit >= tempUpperLimit) {
//                                    tempLowerLimit = tempUpperLimit - 1;
//                                }
//                            }
//                            
//                            if (postData.indexOf("slider2=") >= 0) {
//                                int startPos = postData.indexOf("slider2=") + 8;
//                                int endPos = postData.indexOf("&", startPos);
//                                if (endPos < 0) endPos = postData.length();
//                                String valueStr = postData.substring(startPos, endPos);
//                                tempUpperLimit = valueStr.toInt();
//                                if (tempUpperLimit <= tempLowerLimit) {
//                                    tempUpperLimit = tempLowerLimit + 1;
//                                }
//                            }
//                            
//                            // Update limits if valid
//                            if (tempLowerLimit < tempUpperLimit) {
//                                lowerLimit = tempLowerLimit;
//                                upperLimit = tempUpperLimit;
//                                update_area_label();
//                                update_chart();
//                            }
//                        }
//                        
//                        // Calculate values for display
//                        float area = computeAreaUnderCurve();
//                        float concentration = computeConcentration(area, m_value, c_value);
//                        
////                        // Create JSON data for the chart
////                        String chartData = "[";
////                        int num_points = min(100, upperLimit - lowerLimit);
////                        if (num_points > 0) {
////                            for (int i = 0; i < num_points; i++) {
////                                int index = lowerLimit + i * (upperLimit - lowerLimit) / num_points;
////                                if (index < arraySize) {
////                                    if (i > 0) chartData += ",";
////                                    chartData += "{\"x\":" + String(array1[index]) + ",\"y\":" + String(array2[index]) + "}";
////                                }
////                            }
////                        }
////                        chartData += "]";
////
////                         // Print the first 100 values to Serial
////                        // Serial.println("First 100 values loaded onto the graph:");
////                        if (num_points > 0) {
////                            for (int i = 0; i < num_points; i++) {
////                                int index = lowerLimit + i;
////                                if (index < arraySize) {
////                                    // Serial.print("x: ");
////                                    // Serial.print(array1[index]);
////                                    // Serial.print(", y: ");
////                                    // Serial.println(array2[index]);
////                                }
////                            }
////                        }
////                        
//
//                          // Create JSON data for the chart (only for x in range 2100–2400)         // ############################### CHANGE THESE VALUES FOR CHANGING THE RANGE WHERE I WANNA PLOT #############################
//
//                          String chartData = "[";
//                          int pointCount = 0;
//                          for (int i = 0; i < arraySize; i++) {
//                              if (array1[i] >= 2100 && array1[i] <= 2400) {
//                                  if (pointCount > 0) chartData += ",";
//                                  chartData += "{\"x\":" + String(array1[i]) + ",\"y\":" + String(array2[i]) + "}";
//                                  pointCount++;
//                              }
//                          }
//                          chartData += "]";
//                          
//                          // Print matching values to Serial
//                          // Serial.println("Values loaded onto the graph (x in [2100, 2400]):");
//                          for (int i = 0; i < arraySize; i++) {
//                              if (array1[i] >= 2100 && array1[i] <= 2400) {
//                                  // Serial.print("x: ");
//                                  // Serial.print(array1[i]);
//                                  // Serial.print(", y: ");
//                                  // Serial.println(array2[i]);
//                              }
//                          }                                                                           // ############################### CHANGE THESE VALUES FOR CHANGING THE RANGE WHERE I WANNA PLOT #############################
//
//
//                        // Start sending the HTML content
//                        client.println("<!DOCTYPE html><html>");
//                        client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
//                        client.println("<link rel=\"icon\" href=\"data:,\">");
//                        client.println("<title>Concentration Calculator</title>");
//                        client.println("<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>");
//                        client.println("<style>");
//                        client.println("@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600&display=swap');");
//                        client.println("html, body { font-family: 'Inter', sans-serif; margin: 0; padding: 0; background: #121212; color: #e0e0e0; display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; }");
//                        client.println(".container { text-align: center; width: 90%; max-width: 800px; background: #1e1e1e; padding: 20px; border-radius: 12px; box-shadow: 0px 4px 10px rgba(0,0,0,0.3); }");
//                        client.println("h1 { font-size: 24px; font-weight: 600; margin-bottom: 10px; }");
//                        client.println(".chart-container { width: 100%; height: 60vh; margin: 20px 0; }");
//                        client.println(".slider-container { display: flex; flex-direction: column; align-items: center; width: 100%; margin-top: 20px; }");
//                        client.println(".slider { width: 80%; max-width: 500px; height: 8px; background: linear-gradient(45deg, #3498db, #9b59b6); border-radius: 4px; outline: none; transition: 0.3s; margin: 10px 0; }");
//                        client.println(".slider:hover { transform: scale(1.05); }");
//                        client.println(".label { font-size: 18px; font-weight: 400; text-align: center; color: #ccc; margin-top: 5px; }");
//                        client.println(".btn { background: linear-gradient(45deg, #1abc9c, #16a085); color: white; padding: 12px 24px; border-radius: 8px; cursor: pointer; border: none; font-size: 16px; transition: all 0.3s; margin-top: 20px; display: block; margin-left: auto; margin-right: auto; }");
//                        client.println(".btn:hover { background: linear-gradient(45deg, #16a085, #1abc9c); transform: scale(1.05); }");
//                        client.println(".highlight { color: #f39c12; font-size: 24px; font-weight: bold; margin: 20px 0; padding: 10px; background: rgba(243, 156, 18, 0.1); border-radius: 8px; }");
//                        client.println(".loader { width: 50px; height: 50px; border: 6px solid #ddd; border-top: 6px solid #3498db; border-radius: 50%; animation: spin 1.5s linear infinite; margin: 20px auto; }");
//                        client.println("@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }");
//                        client.println(".controls { width: 100%; max-width: 800px; margin: 20px auto; padding: 15px; background: #2a2a2a; border-radius: 8px; }");
//                        client.println("form { margin: 0; }");
//                        client.println("</style>");
//                        client.println("</head>");
//                        client.println("<body>");
//                        client.println("<div class=\"container\">");
//                        client.println("<div class=\"loader\" id=\"loader\"></div>");
//                        client.println("<div class=\"content\" id=\"mainContent\" style=\"display: none;\">");
//                        client.println("<h1>Concentration Calculator</h1>");
//
//                        // Display the concentration value here
//                        client.println("<div id=\"concentrationLabel\" class=\"highlight\">Concentration: " + String(concentration, 2) + "</div>");
//                        client.println("<div id=\"areaLabel\" class=\"label\">Area: " + String(area, 2) + "</div>");
//
//                        client.println("<div class=\"chart-container\">");
//                        client.println("<canvas id=\"myChart\"></canvas>");
//                        client.println("</div>");
//
//                        // Send the sliders and other controls
//                        client.println("<div class=\"controls\">");
//                        client.println("<form id=\"sliderForm\" method=\"POST\">");
//                        client.println("<div class=\"slider-container\">");
//                        client.println("<input id=\"slider1\" name=\"slider1\" type=\"range\" min=\"0\" max=\"" + String(arraySize - 1) + "\" value=\"" + String(lowerLimit) + "\" class=\"slider\" onchange=\"updateLabels()\" oninput=\"updateLabels()\"/>");
//                        client.println("<div id=\"label1\" class=\"label\">Wavenumber 1: " + String(lowerLimit) + "</div>");
//                        client.println("</div>");
//
//                        client.println("<div class=\"slider-container\">");
//                        client.println("<input id=\"slider2\" name=\"slider2\" type=\"range\" min=\"0\" max=\"" + String(arraySize - 1) + "\" value=\"" + String(upperLimit) + "\" class=\"slider\" onchange=\"updateLabels()\" oninput=\"updateLabels()\"/>");
//                        client.println("<div id=\"label2\" class=\"label\">Wavenumber 2: " + String(upperLimit) + "</div>");
//                        client.println("</div>");
//                        
//                        client.println("<button type=\"submit\" class=\"btn\">Update Graph</button>");
//                        client.println("</form>");
//                        client.println("</div>");
//
//                        client.println("</div>");
//                        client.println("</div>");
//                        
//                        client.println("<script>");
//                        client.println("// Chart data from server");
//                        client.println("const chartData = " + chartData + ";");
//                        
//                        client.println("// Hide loader and show content");
//                        client.println("document.getElementById('loader').style.display = 'none';");
//                        client.println("document.getElementById('mainContent').style.display = 'block';");
//                        
//                        client.println("// Update slider labels");
//                        client.println("function updateLabels() {");
//                        client.println("  const slider1 = document.getElementById('slider1');");
//                        client.println("  const slider2 = document.getElementById('slider2');");
//                        client.println("  document.getElementById('label1').textContent = 'Wavenumber 1: ' + slider1.value;");
//                        client.println("  document.getElementById('label2').textContent = 'Wavenumber 2: ' + slider2.value;");
//                        client.println("  // Ensure slider1 < slider2");
//                        client.println("  if(parseInt(slider1.value) >= parseInt(slider2.value)) {");
//                        client.println("    if(parseInt(slider1.value) >= parseInt(slider2.max) - 1) {");
//                        client.println("      slider1.value = parseInt(slider2.max) - 2;");
//                        client.println("    } else {");
//                        client.println("      slider2.value = parseInt(slider1.value) + 1;");
//                        client.println("    }");
//                        client.println("    document.getElementById('label1').textContent = 'Wavenumber 1: ' + slider1.value;");
//                        client.println("    document.getElementById('label2').textContent = 'Wavenumber 2: ' + slider2.value;");
//                        client.println("  }");
//                        client.println("}");
//                        
//                        client.println("// Create chart");
//                        client.println("const ctx = document.getElementById('myChart').getContext('2d');");
//                        client.println("const myChart = new Chart(ctx, {");
//                        client.println("  type: 'line',");
//                        client.println("  data: {");
//                        client.println("    datasets: [{");
//                        client.println("      label: 'Spectral Data',");
//                        client.println("      data: chartData,");
//                        client.println("      backgroundColor: 'rgba(52, 152, 219, 0.2)',");
//                        client.println("      borderColor: 'rgba(52, 152, 219, 1)',");
//                        client.println("      borderWidth: 2,");
//                        client.println("      pointRadius: 0,");
//                        client.println("      fill: true,");
//                        client.println("      tension: 0.1");
//                        client.println("    }]");
//                        client.println("  },");
//                        client.println("  options: {");
//                        client.println("    responsive: true,");
//                        client.println("    maintainAspectRatio: false,");
//                        client.println("    scales: {");
//                        client.println("      x: {");
//                        client.println("        type: 'linear',");
//                        client.println("        position: 'bottom',");
//                        client.println("        title: {");
//                        client.println("          display: true,");
//                        client.println("          text: 'Wavenumber',");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        },");
//                        client.println("        grid: {");
//                        client.println("          color: 'rgba(255, 255, 255, 0.1)'");
//                        client.println("        },");
//                        client.println("        ticks: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      },");
//                        client.printf("      y: {\n");
//                        client.printf("        min: %.2f,\n", minY);    // dynamic min
//                        client.printf("        max: %.2f,\n", maxY);    // dynamic max
//                        client.println("        title: {");
//                        client.println("          display: true,");
//                        client.println("          text: 'Intensity',");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        },");
//                        client.println("        grid: {");
//                        client.println("          color: 'rgba(255, 255, 255, 0.1)'");
//                        client.println("        },");
//                        client.println("        ticks: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      }");
//                        client.println("    }");
//                        client.println("  }");
//                        client.println("});");
//
//
//                        client.println("    },");
//                        client.println("    plugins: {");
//                        client.println("      legend: {");
//                        client.println("        labels: {");
//                        client.println("          color: '#e0e0e0'");
//                        client.println("        }");
//                        client.println("      },");
//                        client.println("      tooltip: {");
//                        client.println("        mode: 'index',");
//                        client.println("        intersect: false,");
//                        client.println("        backgroundColor: 'rgba(0, 0, 0, 0.8)'");
//                        client.println("      }");
//                        client.println("    }");
//                        client.println("  }");
//                        client.println("});");
//                        
//                        client.println("// Add shaded area between sliders");
//                        client.println("const addAreaPlugin = {");
//                        client.println("  id: 'customCanvasBackgroundColor',");
//                        client.println("  beforeDraw: (chart) => {");
//                        client.println("    const ctx = chart.canvas.getContext('2d');");
//                        client.println("    const slider1 = parseInt(document.getElementById('slider1').value);");
//                        client.println("    const slider2 = parseInt(document.getElementById('slider2').value);");
//                        client.println("    const xScale = chart.scales.x;");
//                        client.println("    const yScale = chart.scales.y;");
//                        client.println("    ");
//                        client.println("    // Find x positions on chart");
//                        client.println("    let xPos1 = null, xPos2 = null;");
//                        client.println("    for(let i = 0; i < chartData.length; i++) {");
//                        client.println("      const x = chartData[i].x;");
//                        client.println("      if (xPos1 === null && x >= slider1) {");
//                        client.println("        xPos1 = xScale.getPixelForValue(x);");
//                        client.println("      }");
//                        client.println("      if (xPos2 === null && x >= slider2) {");
//                        client.println("        xPos2 = xScale.getPixelForValue(x);");
//                        client.println("        break;");
//                        client.println("      }");
//                        client.println("    }");
//                        client.println("    ");
//                        client.println("    if (xPos1 !== null && xPos2 !== null) {");
//                        client.println("      ctx.save();");
//                        client.println("      ctx.fillStyle = 'rgba(243, 156, 18, 0.2)';");
//                        client.println("      ctx.fillRect(xPos1, yScale.top, xPos2 - xPos1, yScale.bottom - yScale.top);");
//                        client.println("      ctx.restore();");
//                        client.println("    }");
//                        client.println("  }");
//                        client.println("};");
//                        client.println("Chart.register(addAreaPlugin);");
//                        
//                        client.println("</script>");
//                        client.println("</body>");
//                        client.println("</html>");
//
//                        // ... (your HTML/JS response code here, as in your original)
//
//                        client.flush();
//                        break; // Done with this client
//                    } else {
//                        currentLine = "";
//                    }
//                } else if (c != '\r') {
//                    currentLine += c;
//                }
//            }
//
//            // Service LVGL and watchdog inside the loop for long client sessions
//            lv_task_handler();
//
//        }
//        client.stop();
//    }
//}
