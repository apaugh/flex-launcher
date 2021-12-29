#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <launcher.h>
#include "util.h"
#ifdef __unix__
#include "platform/unix.h"
#endif
#ifdef _WIN32
#include "platform/win32.h"
#endif
#include "external/ini.h"
#define NANOSVG_IMPLEMENTATION
#include "external/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "external/nanosvgrast.h"


// Initialize default settings
config_t config = {
  .background_image = NULL,
  .slideshow_directory = NULL,
  .title_font_path = NULL,
  .font_size = DEFAULT_FONT_SIZE,
  .title_color.r = DEFAULT_TITLE_COLOR_R,
  .title_color.g = DEFAULT_TITLE_COLOR_G,
  .title_color.b = DEFAULT_TITLE_COLOR_B,
  .title_color.a = DEFAULT_TITLE_COLOR_A,
  .background_mode = MODE_COLOR,
  .background_color.r = DEFAULT_BACKGROUND_COLOR_R,
  .background_color.g = DEFAULT_BACKGROUND_COLOR_G,
  .background_color.b = DEFAULT_BACKGROUND_COLOR_B,
  .background_color.a = 0xFF,
  .icon_size = DEFAULT_ICON_SIZE,
  .default_menu = NULL,
  .highlight_color.r = DEFAULT_HIGHLIGHT_COLOR_R,
  .highlight_color.g = DEFAULT_HIGHLIGHT_COLOR_G,
  .highlight_color.b = DEFAULT_HIGHLIGHT_COLOR_B,
  .highlight_color.a = DEFAULT_HIGHLIGHT_COLOR_A,
  .highlight_rx = DEFAULT_HIGHLIGHT_CORNER_RADIUS,
  .title_padding = -1,
  .max_buttons = DEFAULT_MAX_BUTTONS,
  .icon_spacing = -1,
  .highlight_vpadding = -1,
  .highlight_hpadding = -1,
  .title_opacity[0] = '\0',
  .highlight_opacity[0] = '\0',
  .button_centerline[0] = '\0',
  .icon_spacing_str[0] = '\0',
  .scroll_indicators = DEFAULT_SCROLL_INDICATORS,
  .scroll_indicator_color.r = DEFAULT_SCROLL_INDICATOR_COLOR_R,
  .scroll_indicator_color.g = DEFAULT_SCROLL_INDICATOR_COLOR_G,
  .scroll_indicator_color.b = DEFAULT_SCROLL_INDICATOR_COLOR_B,
  .scroll_indicator_color.a = DEFAULT_SCROLL_INDICATOR_COLOR_A,
  .scroll_indicator_opacity[0] = '\0',
  .title_oversize_mode = MODE_TEXT_TRUNCATE,
  .reset_on_back = DEFAULT_RESET_ON_BACK,
  .esc_quit = DEFAULT_ESC_QUIT,
  .gamepad_enabled = DEFAULT_GAMEPAD_ENABLED,
  .gamepad_device = DEFAULT_GAMEPAD_DEVICE,
  .gamepad_mappings_file = NULL,
  .on_launch = MODE_ON_LAUNCH_HIDE,
  .debug = false,
  .exe_path = NULL,
  .first_menu = NULL,
  .gamepad_controls = NULL,
  .num_menus = 0,
  .slideshow_image_duration = DEFAULT_SLIDESHOW_IMAGE_DURATION,
  .slideshow_transition_time = DEFAULT_SLIDESHOW_TRANSITION_TIME
};

state_t state = {
  .screen_updates = false,
  .slideshow_transition = false
};

// Global variables
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_RWops *log_file = NULL;
TTF_Font *title_font = NULL; // Font of the button title text
menu_t *default_menu = NULL;
menu_t *current_menu = NULL; // Current selected menu
entry_t *current_entry = NULL; // Current selected entry
ticks_t ticks;
SDL_GameController *gamepad = NULL;
int delay_period;
int repeat_period; 
scroll_t *scroll = NULL;
slideshow_t *slideshow = NULL;
SDL_Texture *background_texture = NULL; // Background texture (image only)
NSVGrasterizer *rasterizer = NULL;
highlight_t *highlight = NULL; // Pointer containing highlight texture and coordinates
bool updates; // Bool to update the screen at the end of the main loop
geometry_t geo; // Struct containing screen geometry for the current page of buttons
bool quit = false;

// A function to initialize SDL
int init_sdl()
{
  SDL_DisplayMode display_mode;
  
  // Set flags, hints
  int sdl_flags = SDL_INIT_VIDEO;
  int img_flags = IMG_INIT_PNG | 
                  IMG_INIT_JPG | 
                  IMG_INIT_WEBP;
  #ifdef __unix__
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
  #endif
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
  if (config.gamepad_enabled) {
    sdl_flags |= SDL_INIT_GAMECONTROLLER;
    delay_period = GAMEPAD_REPEAT_DELAY / POLLING_PERIOD;
    repeat_period = GAMEPAD_REPEAT_INTERVAL / POLLING_PERIOD; 
  }

  // Initialize SDL
  if (SDL_Init(sdl_flags) < 0)
  {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Could not initialize SDL\n%s\n", 
               SDL_GetError());
    return 1;
  }

  // Create window, hide mouse cursor
  window = SDL_CreateWindow(PROJECT_NAME, SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          0,
                                          0,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP | 
                                          SDL_WINDOW_BORDERLESS);
  if (window == NULL) {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Could not create SDL Window\n%s\n", 
               SDL_GetError());
    return 1;
  }
  SDL_ShowCursor(SDL_DISABLE);

  // Create HW accelerated renderer, get screen resolution
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_GetCurrentDisplayMode(0, &display_mode);
  geo.screen_width = display_mode.w;
  geo.screen_height = display_mode.h;
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
  if (renderer == NULL) {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Could not initialize renderer\n%s\n", 
               SDL_GetError());
    return 1;
  }

  // Set background color
  set_draw_color();

  // Initialize SDL_image
  if (!(IMG_Init(img_flags) & img_flags)) {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Could not initialize SDL_image\n%s\n", 
               IMG_GetError());
    return 1;
  }
  return 0;
}

void set_draw_color()
{
  if (config.background_mode == MODE_COLOR) {
    SDL_SetRenderDrawColor(renderer,
                            config.background_color.r,
                            config.background_color.g,
                            config.background_color.b,
                            config.background_color.a);
  }
  else {
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
  }
}

// A function to initialize SDL's TTF subsystem
int init_ttf()
{
  // Initialize SDL_ttf
  if (TTF_Init() == -1) {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Could not initialize SDL_ttf\n%s\n", 
               TTF_GetError());
    return 1;
   }

  // Load user specified font
  if (config.title_font_path != NULL) {
    title_font = TTF_OpenFont(config.title_font_path, config.font_size);
  }

  // Try to find default font if user specified font is not found
  if (title_font == NULL){
    output_log(LOGLEVEL_ERROR, "Error: Could not initialize font from config file\n");
    char *prefixes[2];
    char fonts_exe_buffer[MAX_PATH_BYTES];
    prefixes[0] = join_paths(fonts_exe_buffer, 2, config.exe_path, PATH_FONTS_EXE);
    #ifdef __unix__
    prefixes[1] = PATH_FONTS_SYSTEM;
    #else
    prefixes[1] = PATH_FONTS_RELATIVE;
    #endif
    char *default_font = find_file(FILENAME_DEFAULT_FONT, 2, prefixes);

    // Replace user font with default in config
    if (default_font != NULL) {
      title_font = TTF_OpenFont(default_font,config.font_size);
      free(config.title_font_path);
      copy_string(&config.title_font_path, default_font);
    }
    if(title_font == NULL) {
      output_log(LOGLEVEL_FATAL, "Fatal Error: Could not load default font\n");
      return 1;
    }
  }

  // Get font height for geometry calculations
  TTF_SizeUTF8(title_font,"TEST STRING",NULL,&geo.font_height);
  return 0;
}

// A function to initalize SVG rasterization
int init_svg()
{
  rasterizer = nsvgCreateRasterizer();
  if (rasterizer == NULL) {
    output_log(LOGLEVEL_FATAL, "Fatal Error: Could not initialize SVG rasterizer.\n");
    return 1;
  }
  return 0;
}

// A function to close subsystems and free memory before quitting
void cleanup()
{
  // Destroy renderer and window
  if (renderer != NULL) {
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
  }
  if (window != NULL) {
    SDL_DestroyWindow(window);
    window = NULL;
  }

  // Destroy SVG rasterizer
  nsvgDeleteRasterizer(rasterizer);

  // Quit SDL subsystems
  SDL_Quit();
  IMG_Quit();
  TTF_Quit();

  // Close log file if open
  if (log_file != NULL) {
    SDL_RWclose(log_file);
  }

  // Free dynamically allocated memory
  free(config.default_menu);
  free(config.background_image);
  free(config.title_font_path);
  free(config.exe_path);
  free(highlight);
  free(scroll);

  if (config.background_mode == MODE_SLIDESHOW) {
    quit_slideshow();
  }

  // Free menu and entry linked lists
  entry_t *entry = NULL;
  entry_t *tmp_entry = NULL;
  menu_t *menu = config.first_menu;
  menu_t *tmp_menu = NULL;
  for (int i = 0; i < config.num_menus; i++) {
    free(menu->name);
    entry = menu->first_entry;
    for(int j = 0; j < menu->num_entries; j++) {
      free(entry->title);
      free(entry->icon_path);
      free(entry->cmd);
      tmp_entry = entry;
      entry = entry->next;
      free(tmp_entry);
    }
    tmp_menu = menu;
    menu = menu->next;
    free(tmp_menu);
  }

  // Free gamepad control linked list
  gamepad_control_t *tmp_gamepad = NULL;
  for (gamepad_control_t *i = config.gamepad_controls; i != NULL; i = i->next) {
    free(tmp_gamepad);
    free(i->label);
    free(i->cmd);
    tmp_gamepad = i;
  }
  free(tmp_gamepad);
}

// A function to handle key presses from keyboard
void handle_keypress(SDL_Keysym *key)
{
  if (config.debug) {
    output_log(LOGLEVEL_DEBUG, 
               "Key %s (%X) detected\n", 
               SDL_GetKeyName(key->sym),
               key->sym);
  }

  // Check keys
  if (key->sym == SDLK_ESCAPE && config.esc_quit) {
    quit = true;
  }
  else if (key->sym == SDLK_LEFT) {
    move_left();
  }
  else if (key->sym == SDLK_RIGHT) {
    move_right();
  }
  else if (key->sym == SDLK_RETURN) {
    output_log(LOGLEVEL_DEBUG, 
               "Selected Entry:\n"
               "Title: %s\n"
               "Icon Path: %s\n"
               "Command: %s\n", 
               current_entry->title, 
               current_entry->icon_path, 
               current_entry->cmd);
    
    execute_command(current_entry->cmd);
  }
  else if (key->sym == SDLK_BACKSPACE) {
    load_back_menu(current_menu);
  }
}

// A function to calculate the total width of all screen objects
unsigned int calculate_width(int buttons, int icon_spacing, int icon_size, int highlight_hpadding)
{
  return (buttons - 1)*icon_spacing + buttons*icon_size + 2*highlight_hpadding;
}

void quit_slideshow()
{
  // Free allocated image paths
  for (int i = 0; i < slideshow->num_images; i++) {
    free(slideshow->images[i]);
  }
  free(slideshow);
  
  //config.background_mode = MODE_COLOR;
  //output_log(LOGLEVEL_ERROR, "Error: Couldn't load background image, defaulting to color background\n");
  //set_draw_color();

}

void init_slideshow()
{
  if (!directory_exists(config.slideshow_directory)) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Slideshow directory %s does not exist\n"
               "Switching to color background mode\n",
               config.slideshow_directory);
    config.background_mode = MODE_COLOR;
    set_draw_color();
    return;
  }
  // Allocate and initialize slideshow struct
  slideshow = malloc(sizeof(slideshow_t));
  slideshow->i = -1;
  slideshow->num_images = 0;
  slideshow->transition_texture = NULL;
  slideshow->transition_alpha = 0.0f;
  slideshow->transition_change_rate = 255.0f / ((float) config.slideshow_transition_time / (float) POLLING_PERIOD);

  // Find background images from directory
  int num_images = scan_slideshow_directory(slideshow, config.slideshow_directory);
  
  // Handle errors
  if (num_images == 0) {
    output_log(LOGLEVEL_ERROR, 
               "Error: No images found in slideshow directory %s\n"
               "Changing background mode to color\n", 
               config.slideshow_directory);
    quit_slideshow();
  } 
  else if (num_images == 1) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Only one image found in slideshow directory %s\n"
               "Changing background mode to single image\n", 
               config.slideshow_directory);
  }

  // Generate array of random numbers for image order, load first image
  else {
    random_array(slideshow->order, slideshow->num_images);
    background_texture = load_next_slideshow_background();
    if (config.debug) {
      debug_slideshow(slideshow);
    }
  }
}

SDL_Texture *load_next_slideshow_background()
{
  SDL_Surface *surface = NULL;
  SDL_Texture *texture = NULL;
  int initial_index = slideshow->i;
  int attempts = 0;
  do {
    // Increment slideshow background index and load background
    (slideshow->i)++;
    if (slideshow->i >= slideshow->num_images) {
      slideshow->i = 0;
    }
    surface = IMG_Load(slideshow->images[slideshow->order[slideshow->i]]);
    
    // If the loaded image has no alpha channel (e.g. JPEG), create one 
    // so that we can have transparency for the background transition
    if (surface->format->format == SDL_PIXELFORMAT_RGB24) {
      SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0,
        surface->w,
        surface->h,
        32,
        SDL_PIXELFORMAT_ARGB8888);
      Uint32 color = SDL_MapRGBA(tmp->format, 0, 0, 0, 0xFF);
      SDL_FillRect(tmp, NULL, color);
      SDL_BlitSurface(surface, NULL, tmp, NULL);
      SDL_FreeSurface(surface);
      surface = tmp;
      attempts++;
    } 
  } while (surface == NULL && slideshow->i != initial_index && attempts < slideshow->num_images);
  
  // Switch to color background mode if we failed to load any image from the array
  if (surface == NULL) {
    output_log(LOGLEVEL_ERROR, 
      "Could not load images from slideshow directory %s\n"
      "Changing background to color mode\n", 
      config.slideshow_directory);
    quit_slideshow();
  }

  // If only one image in the entire slideshow array was valid, switch to
  // single image background mode
  else if (slideshow->i == initial_index && surface != NULL) {
    background_texture = SDL_CreateTextureFromSurface(renderer, surface);
    config.background_mode = MODE_IMAGE;
  }

  // Loading was successful, convert to texture
  else {
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    
    // Start as transparent except first image
    if (initial_index > -1) {
      SDL_SetTextureAlphaMod(texture, 0);
    }
  }
  return texture;
}

// A function to load a texture from a file OR existing SDL surface
SDL_Texture *load_texture(char *path, SDL_Surface *surface)
{
  SDL_Texture *texture = NULL;
  SDL_Surface *loaded_surface = NULL;

    if (surface == NULL) {
      loaded_surface = IMG_Load(path);
    }
    else {
      loaded_surface = surface;
    }

    if (loaded_surface == NULL) {
        output_log(LOGLEVEL_ERROR, 
                  "Error: Could not load image %s\n%s\n", 
                  path, 
                  IMG_GetError());
    }
    else {
        //Convert surface to screen format
        texture = SDL_CreateTextureFromSurface(renderer, loaded_surface);
        if (texture == NULL) {
            output_log(LOGLEVEL_ERROR, "Error: Could not create texture from %s\n%s", 
                       path, 
                       SDL_GetError());
        }

        //Get rid of old loaded surface
        SDL_FreeSurface(loaded_surface);
    }
    return texture;
}

// A function to rasterize an SVG from a file OR from xml text buffer
SDL_Texture *rasterize_svg(char *filename, char *xml, int w, int h)
{
  NSVGimage *image = NULL;
  unsigned char *pixel_buffer = NULL;
  int width, height, pitch;
  float scale;

  // Parse SVG to NSVGimage struct
  if (filename == NULL) {
    image = nsvgParse(xml, "px", 96.0f);
  }
  else {
    image = nsvgParseFromFile(filename, "px", 96.0f);
  }
  if (image == NULL) {
    output_log(LOGLEVEL_ERROR, "Error: could not open SVG image.\n");
    return NULL;
  }

  // Calculate scaling
  if (w == -1 && h == -1) {
    width = (int) image->width;
    height = (int) image->height;
    scale = 1.0f;
  }
  else {
    width = w;
    height = h;
    scale = (float) w / (float) image->width; // Assuming aspect ratio is conserved
  }

  // Allocate memory
  pitch = 4*width;
  pixel_buffer = malloc(4*width*height);
  if (pixel_buffer == NULL) {
    output_log(LOGLEVEL_ERROR, "Error: Could not alloc SVG pixel buffer.\n");
    return NULL;
  }

  // Rasterize image
  nsvgRasterize(rasterizer, image, 0, 0, scale, pixel_buffer, width, height, pitch);
  SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(pixel_buffer,
                                                  width,
                                                  height,
                                                  32,
                                                  pitch,
                                                  COLOR_MASKS);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  free(pixel_buffer);
  SDL_FreeSurface(surface);
  nsvgDelete(image);
  return texture;
}

// A function to render the highlight for the buttons
SDL_Texture *render_highlight(int width, int height, int rx)
{
  // Insert user config variables into SVG-formatted text buffer
  char buf[1024];
  sprintf(buf, SVG_HIGHLIGHT, width, height, width, height, rx);
  
  // Rasterize the SVG
  SDL_Texture *texture = rasterize_svg(NULL, buf, -1, -1);
  
  // Set color
  SDL_SetTextureColorMod(texture,
                         config.highlight_color.r,
                         config.highlight_color.g,
                         config.highlight_color.b);
  SDL_SetTextureAlphaMod(texture,config.highlight_color.a);
  return texture;
}

// A function to render title text for an entry
SDL_Texture *render_text(entry_t *entry)
{
  TTF_Font *output_font = NULL;
  TTF_Font *reduced_font = NULL; // Font for Shrink text oversize mode
  int max_width = config.icon_size;
  int w, h;

  // Copy entry title to new buffer buffer
  char *title;
  copy_string(&title, entry->title);

  // Calculate size of the rendered title
  int title_length = strlen(title);
  TTF_SizeUTF8(title_font,title,&w,&h);

  // If title is too large to fit
  if (w > max_width) {

    // Truncate mode:
    if (config.title_oversize_mode == MODE_TEXT_TRUNCATE) {
      utf8_truncate(title, w, max_width);
      TTF_SizeUTF8(title_font,title,&w,&h);
    }

    // Shrink mode:
    else if (config.title_oversize_mode == MODE_TEXT_SHRINK) {
      int font_size = config.font_size - 1;
      reduced_font = TTF_OpenFont(config.title_font_path, font_size);
      TTF_SizeUTF8(reduced_font,title,&w,&h);

      // Keep trying smaller font until it fits
      while (w > max_width && font_size > 0) {
        TTF_CloseFont(reduced_font);
        reduced_font = NULL;
        font_size--;
        reduced_font = TTF_OpenFont(config.title_font_path, font_size);
        TTF_SizeUTF8(reduced_font,title,&w,&h);
      }

      // Set vertical offset so reduced font title remains vertically centered
      // with other titles
      if (font_size) {
        output_font = reduced_font;
        entry->title_offset = (geo.font_height - h) / 2;
      }
      else {
        reduced_font = NULL;
      }
    }
  }
  
  // Set geometry
  entry->text_rect.w = w;
  entry->text_rect.h = h;
  if (reduced_font == NULL) {
    output_font = title_font;
  }

  // Render texture
  SDL_Surface *text_surface = TTF_RenderUTF8_Blended(output_font,
                                                    title,
                                                    config.title_color);
  SDL_Texture *text_texture = SDL_CreateTextureFromSurface(renderer,text_surface);
  SDL_FreeSurface(text_surface);
  if (reduced_font != NULL) {
    TTF_CloseFont(reduced_font);
  }
  free(title);
  return text_texture;
}

// A function to advance X spaces in the entry linked list (left or right)
entry_t *advance_entries(entry_t *entry, int spaces, mode direction)
{
  if (direction == DIRECTION_LEFT) {
    for (int i = 0; i < spaces; i++) {
      entry = entry->previous;
    }
  }
  else if (direction == DIRECTION_RIGHT) {
    for (int i = 0; i < spaces; i++) {
      entry = entry->next;
    }
  }
  return entry;
}

void render_scroll_indicators()
{
  // Calcuate the geometry
  scroll = malloc(sizeof(scroll_t));
  scroll->texture = NULL;
  int scroll_indicator_size = geo.screen_height / 10; // ~10% of screen height
  scroll->rect_right.w = scroll_indicator_size;
  scroll->rect_right.h = scroll_indicator_size;
  scroll->rect_left.w = scroll_indicator_size;
  scroll->rect_left.h = scroll_indicator_size;

  // Find scroll indicator file
  char *prefixes[2];
  char assets_exe_buffer[MAX_PATH_BYTES];
  prefixes[0] = join_paths(assets_exe_buffer, 2, config.exe_path, PATH_ASSETS_EXE);
  #ifdef __unix__
  prefixes[1] = PATH_ASSETS_SYSTEM;
  #else
  prefixes[1] = PATH_ASSETS_RELATIVE;
  #endif
  char *scroll_indicator_path = find_file(FILENAME_SCROLL_INDICATOR, 2, prefixes);

  if (scroll_indicator_path == NULL) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Could not find scroll indicator SVG, disabling feature\n");
    config.scroll_indicators = false;
  }
  else {
    output_log(LOGLEVEL_DEBUG, "Scroll indicator found: %s\n", 
               scroll_indicator_path);

    // Render the SVG
    scroll->texture = rasterize_svg(scroll_indicator_path,
                                    NULL,
                                    scroll_indicator_size,
                                    scroll_indicator_size);
    free(scroll_indicator_path);
    if (scroll->texture == NULL) {
      output_log(LOGLEVEL_ERROR, "Error: Could not render scroll indicator, disabling feature\n");
      config.scroll_indicators = false;
    }
    else {

      // Calculate screen position based on margin macro
      scroll->rect_right.y = (int) ((1.0F - SCROLL_INDICATOR_MARGIN)
                                    *(float) geo.screen_height) - scroll_indicator_size;
      scroll->rect_right.x = geo.screen_width - (geo.screen_height - scroll->rect_right.y);
      scroll->rect_left.y = scroll->rect_right.y;
      scroll->rect_left.x = geo.screen_width - (scroll->rect_right.x + scroll_indicator_size);

      // Set color
      SDL_SetTextureColorMod(scroll->texture,
                              config.scroll_indicator_color.r,
                              config.scroll_indicator_color.g,
                              config.scroll_indicator_color.b);
      SDL_SetTextureAlphaMod(scroll->texture,
                              config.scroll_indicator_color.a);
    }
  }
}

// A function to load a menu by name OR existing menu struct
int load_menu(char *menu_name, menu_t *menu, bool set_back_menu, bool reset_position)
{
  int buttons;
  menu_t *previous_menu = current_menu;

  // Get the menu struct from menu name
  if (menu_name != NULL) {
    current_menu = get_menu(menu_name, config.first_menu);
  }
  else {
    current_menu = menu;
  }

  // Return error if the menu doesn't exist in the config file
  if (current_menu == NULL) {
    current_menu = previous_menu;
    if (menu_name != NULL) {
      output_log(LOGLEVEL_ERROR, 
                 "Error: Menu \"%s\" not found in config file\n", 
                 menu_name);
    }
    return 1;
  }

  output_log(LOGLEVEL_DEBUG, "Loading menu \"%s\"\n", current_menu->name);

  // Return error if the menu doesn't contain entires
  if (current_menu->num_entries == 0) {
    output_log(LOGLEVEL_ERROR, 
               "Error: No valid entries found for Menu \"%s\"", 
               current_menu->name);
    return 1;
  }

  // Render the menu if not already rendered
  if (current_menu->rendered == false) {
    render_buttons(current_menu);
  }

  // Set menu properties
  if (set_back_menu) {
    current_menu->back = previous_menu;
  }

  if (reset_position) {
    current_entry = current_menu->first_entry;
    current_menu->root_entry = current_entry;
    current_menu->highlight_position = 0;
    current_menu->page = 0;
  }
  else {
    current_entry = current_menu->last_selected_entry;
  }

  buttons = current_menu->num_entries - (current_menu->page)*config.max_buttons;
  if (buttons > config.max_buttons) {
    buttons = config.max_buttons;
  }
  
  // Recalculate the screen geometry
  calculate_geometry(current_menu->root_entry, buttons);
  //if (config.debug) {
  //  debug_button_positions(current_menu->root_entry, current_menu, &geo);
  //}
  highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
  highlight->rect.y = current_entry->icon_rect.y - config.highlight_vpadding;
  
  // Output to screen
  state.screen_updates = true;
  return 0;
}

// A function to calculate the layout of the buttons
void calculate_geometry(entry_t *entry, int buttons)
{
  // Calculate proper spacing
  int button_height = config.icon_size + config.title_padding + geo.font_height;
  geo.x_margin = (geo.screen_width - config.icon_size*buttons -
                  buttons*config.icon_spacing + config.icon_spacing) / 2;
  geo.x_advance = config.icon_size + config.icon_spacing;
  geo.num_buttons = buttons;

  // Assign values to entries
  for (int i = 0; i < geo.num_buttons; i++) {
      entry->icon_rect.x = geo.x_margin + i*geo.x_advance;
      entry->icon_rect.y = geo.y_margin;
      entry->icon_rect.w = config.icon_size;
      entry->icon_rect.h = config.icon_size;
      entry->text_rect.x = entry->icon_rect.x +
                          (entry->icon_rect.w - entry->text_rect.w) / 2;
      entry->text_rect.y = entry->icon_rect.y +
                          config.icon_size + entry->title_offset + config.title_padding;
      entry = entry->next;
  }
}

// A function to render all buttons (icon and text) for a menu
void render_buttons(menu_t *menu)
{
  entry_t *entry;
  for (entry = menu->first_entry; entry != NULL; entry = entry->next) {
    entry->icon = load_texture(entry->icon_path, NULL);
    entry->title_texture = render_text(entry);
  }
  menu->rendered = true;
}

// A function to output all visible buttons to the renderer
void draw_buttons(entry_t *entry)
{
  for (int i = 0; i < geo.num_buttons; i++) {
    SDL_RenderCopy(renderer,entry->icon,NULL,&entry->icon_rect);
    SDL_RenderCopy(renderer,entry->title_texture,NULL,&entry->text_rect);
    entry = entry-> next;
  }
}

// A function to move the selection left when clicked by user
void move_left()
{
  // If we are not in leftmost position, move highlight left
  if (current_menu->highlight_position > 0) {
    highlight->rect.x -= geo.x_advance;
    current_menu->highlight_position--;
    current_entry = current_entry->previous;
    state.screen_updates = true;
  }

  // If we are in leftmost position, but there is a previous page, load the previous page
  else if (current_menu->highlight_position == 0 && current_menu->page > 0) {
    int buttons = config.max_buttons;
    current_entry = current_entry->previous;
    current_menu->root_entry = advance_entries(current_menu->root_entry,buttons,DIRECTION_LEFT);
    calculate_geometry(current_menu->root_entry, buttons);
    highlight->rect.x = current_entry->icon_rect.x
                        - config.highlight_hpadding;
    current_menu->page--;
    current_menu->highlight_position = buttons - 1;
    //if (config.debug) {
    //  debug_button_positions(current_menu->root_entry, current_menu, &geo);
    //}
    state.screen_updates = true;
  }
}

// A function to move the selection right when clicked by the user
void move_right()
{
  // If we are not in the rightmost position, move highlight right
  if (current_menu->highlight_position < (geo.num_buttons - 1)) {
    highlight->rect.x += geo.x_advance;
    current_menu->highlight_position++;
    current_entry = current_entry->next;
    state.screen_updates = true;
  }

  // If we are in the rightmost postion, but there are more entries in the menu, load next page
  else if (current_menu->highlight_position + current_menu->page*config.max_buttons <
  (current_menu->num_entries - 1)) {
    int buttons = current_menu->num_entries
                  - (current_menu->page + 1)*config.max_buttons;
    if (buttons > config.max_buttons) {
      buttons = config.max_buttons;
    }
    current_entry = current_entry->next;
    current_menu->root_entry = current_entry;
    calculate_geometry(current_menu->root_entry, buttons);
    highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
    current_menu->page++;
    current_menu->highlight_position = 0;
    //if (config.debug) {
    //  debug_button_positions(current_menu->root_entry, current_menu, &geo);
    //}
    state.screen_updates = true;
  }
}

// A function to load a submenu
void load_submenu(char *submenu)
{
  current_menu->last_selected_entry = current_entry;
  load_menu(submenu, NULL, true, true);
}

// A function to load the previous menu
void load_back_menu(menu_t *menu)
{
  load_menu(NULL, menu->back, false, config.reset_on_back);
}

// A function to update the screen with all visible textures
void draw_screen()
{
  // Draw background
  if (config.background_mode == MODE_COLOR) {
    SDL_RenderClear(renderer);
  }
  else {
    SDL_RenderCopy(renderer, background_texture, NULL, NULL);
  }

  if (config.background_mode == MODE_SLIDESHOW && state.slideshow_transition) {
    SDL_RenderCopy(renderer, slideshow->transition_texture, NULL, NULL);
  }

  // Draw scroll indicators
  if (config.scroll_indicators &&
  (current_menu->page*config.max_buttons + geo.num_buttons) <= (current_menu->num_entries - 1)) {
    SDL_RenderCopy(renderer,scroll->texture,NULL,&scroll->rect_right);
  }
  if (config.scroll_indicators && current_menu->page > 0) {
    SDL_RenderCopyEx(renderer,scroll->texture,NULL,&scroll->rect_left,0,NULL,SDL_FLIP_HORIZONTAL);
  }

  // Draw highlight
  SDL_RenderCopy(renderer,
                 highlight->texture,
                 NULL,
                 &highlight->rect);

  // Draw buttons
  draw_buttons(current_menu->root_entry);

  // Output to screen
  SDL_RenderPresent(renderer);
  state.screen_updates = false;
}

// A function to make sure all settings are in their correct range
void validate_settings()
{
  // Reduce number of buttons if they can't all fit on screen
  if (config.icon_size * config.max_buttons > geo.screen_width) {
    int i;
    for (i = config.max_buttons; i * config.icon_size > geo.screen_width && i > 0; i--);
    output_log(LOGLEVEL_ERROR, "Error: Not enough screen space for %i buttons, reducing to %i\n", 
                               config.max_buttons, 
                               i);
    config.max_buttons = i; 
  }

  // Convert % opacity settings to 0-255
  if (strlen(config.title_opacity)) {
    int opacity = convert_percent(config.title_opacity,0xFF);
    if (opacity != -1) {
      config.title_color.a = (Uint8) opacity;
    }
  }
  if (strlen(config.highlight_opacity)) {
    int opacity = convert_percent(config.highlight_opacity,0xFF);
    if (opacity != -1) {
      config.highlight_color.a = (Uint8) opacity;
    }
  }
  if (strlen(config.scroll_indicator_opacity)) {
    int opacity = convert_percent(config.scroll_indicator_opacity,0xFF);
    if (opacity != -1) {
      config.scroll_indicator_color.a = (Uint8) opacity;
    }
  }

  // Set default IconSpacing if none is in the config file
  if (config.icon_spacing < 0) {
    config.icon_spacing = geo.screen_width / 20;
  }

  // Convert % for IconSpacing setting
  if (strlen(config.icon_spacing_str)) {
    int icon_spacing = convert_percent(config.icon_spacing_str,geo.screen_width);
    if (icon_spacing < 0) {
      config.icon_spacing = 0;
    }
    else {
      config.icon_spacing = icon_spacing;
    }
  }
  
  // Reduce highlight hpadding to prevent overlaps
  if (config.highlight_hpadding > (config.icon_spacing / 2)) {
    config.highlight_hpadding = config.icon_spacing / 2;
  }

  // Reduce icon spacing and highlight padding if too large to fit onscreen
  unsigned int required_length = calculate_width(config.max_buttons,
                                                 config.icon_spacing,
                                                 config.icon_size,
                                                 config.highlight_hpadding);
  int highlight_hpadding = config.highlight_hpadding;
  int icon_spacing = config.icon_spacing;
  for (int i = 0; i < 100 && required_length > geo.screen_width; i++) {
    if (highlight_hpadding > 0) {
      highlight_hpadding = (highlight_hpadding * 9) / 10;
    }
    if (icon_spacing > 0) {
      icon_spacing = (icon_spacing * 9) / 10;
    }
    required_length = calculate_width(config.max_buttons,icon_spacing,config.icon_size,highlight_hpadding);
  }
  if (config.highlight_hpadding != highlight_hpadding) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Highlight padding value %i too large to fit screen, shrinking to %i\n",
               config.highlight_hpadding, 
               highlight_hpadding);
    config.highlight_hpadding = highlight_hpadding;
  }
  if (config.icon_spacing != icon_spacing) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Icon spacing value %i too large to fit screen, shrinking to %i\n",
               config.icon_spacing, 
               icon_spacing);
    config.icon_spacing = icon_spacing;
  }

  // Make sure title padding is in valid range
  if (config.title_padding < 0 || config.title_padding > config.icon_size / 2) {
    int title_padding = config.icon_size / 10;
    output_log(LOGLEVEL_ERROR, 
               "Error: Text padding value %i invalid, changing to %i\n",
               config.title_padding, 
               title_padding);
    config.title_padding = title_padding;
  }

  // Calculate y coordinates for buttons from centerline setting
  if (strlen(config.button_centerline)) {
    int button_centerline;
    int button_height = config.icon_size + config.title_padding + geo.font_height;
    if (strstr(config.button_centerline,"%") != NULL) {
      button_centerline = convert_percent(config.button_centerline,geo.screen_height);
      if (button_centerline == -1) {
        button_centerline = geo.screen_height / 2;
      }
    }
    else {
      button_centerline = atoi(config.button_centerline);
      if (button_centerline == 0 && strcmp(config.button_centerline,"0")) {
          button_centerline = geo.screen_height / 2;
        }
    }
    if (button_centerline < geo.screen_height / 4) {
      button_centerline = geo.screen_height / 4;
    }
    else if (button_centerline > 3*geo.screen_height / 4) {
      button_centerline = 3*geo.screen_height / 4;
    }
    geo.y_margin = button_centerline - button_height / 2;
  }
  else {
    int button_height = config.icon_size + config.title_padding + geo.font_height;
    geo.y_margin = geo.screen_height / 2 - button_height / 2;
  }
}

// A function to execute the user's command
void execute_command(char *command)
{
  if (strlen(command) == 0) {
    return;
  }

  // Copy command into separate buffer
  char *cmd;
  copy_string(&cmd, command);

  // Parse .desktop file (Linux only)
  #ifdef __unix__
    char *desktop_exec = NULL;
    int desktop = parse_desktop_file(cmd, &desktop_exec);
    if (desktop == DESKTOP_SUCCESS) {
      free(cmd);
      cmd = desktop_exec;
    }
    else if (desktop == DESKTOP_ERROR) {
      free(cmd);
      return;
    }
  #endif

  // Parse special commands
  if (cmd[0] == ':') {
    char *delimiter = " ";
    char *special_command = strtok(cmd,delimiter);
    if (!strcmp(special_command, SCMD_SUBMENU)) {
      char *submenu = strtok(NULL,delimiter);
      load_submenu(submenu);
    }
    else if (!strcmp(special_command, SCMD_LEFT)) {
      move_left();
    }
    else if (!strcmp(special_command, SCMD_RIGHT)) {
      move_right();
    }
    else if (!strcmp(special_command, SCMD_SELECT)) {
      execute_command(current_entry->cmd);
    }
    else if (!strcmp(special_command, SCMD_HOME)) {
      load_menu(NULL, default_menu, false, true);
    }
    else if (!strcmp(special_command, SCMD_BACK)) {
      load_back_menu(current_menu);
    }
    else if (!strcmp(special_command, SCMD_QUIT)) {
      quit = true;
    }
    else if (!strcmp(special_command, SCMD_SHUTDOWN)) {
      system(CMD_SHUTDOWN);
    }
    else if (!strcmp(special_command, SCMD_RESTART)) {
      system(CMD_RESTART);
    }
    else if (!strcmp(special_command, SCMD_SLEEP)) {
      system(CMD_SLEEP);
    }
  }

  // Launch external application
  else {

    // Perform prelaunch behavior from OnLaunch setting
    if (config.on_launch == MODE_ON_LAUNCH_HIDE) {
      SDL_HideWindow(window);
    }
    else if (config.on_launch == MODE_ON_LAUNCH_BLANK) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
      SDL_RenderClear(renderer);
      SDL_RenderPresent(renderer);
    }
    #ifdef _WIN32
    cmd = convert_cmd(cmd);
    #endif

    // Launch application
    system(cmd);

    if (config.on_launch == MODE_ON_LAUNCH_HIDE) {
      SDL_ShowWindow(window);
    }

    SDL_PumpEvents();
  }
  free(cmd);
}

// A function to connect to a gamepad
void connect_gamepad(int device_index)
{
  gamepad = SDL_GameControllerOpen(device_index);
  if (gamepad == NULL) {
    output_log(LOGLEVEL_ERROR, 
               "Error: Could not open gamepad at device index %i\n", 
               config.gamepad_device);
    return;
  }
  if (config.debug) {
    char *mapping = SDL_GameControllerMapping(gamepad);
    output_log(LOGLEVEL_DEBUG, 
               "Gamepad Mapping:\n%s\n", 
               mapping);
    SDL_free(mapping);
  }
}

// A function to poll the connected gamepad for commands
void poll_gamepad()
{
  int value_multiplier; // Handles positive or negative axis
  for (gamepad_control_t *i = config.gamepad_controls; i != NULL; i = i->next) {
    
    // Check if axis value exceeds dead zone
    if (i->type == TYPE_AXIS_POS || i->type == TYPE_AXIS_NEG) {
      if (i->type == TYPE_AXIS_POS) {
        value_multiplier = 1;
      }
      else if (i->type == TYPE_AXIS_NEG) {
        value_multiplier = -1;
      }
      if (value_multiplier*SDL_GameControllerGetAxis(gamepad, i->index) > GAMEPAD_DEADZONE) {
        i->repeat++;
      }
      else {
        i->repeat = 0;
      }
    }

    // Check buttons
    else if (i->type == TYPE_BUTTON) {
      if (SDL_GameControllerGetButton(gamepad, i->index)) {
        i->repeat++;
      }
      else {
        i->repeat = 0;
      }
    }

    // Execute command if first press or valid repeat
    if (i->repeat == 1) {
      output_log(LOGLEVEL_DEBUG, "Gamepad %s detected\n", i->label);
      execute_command(i->cmd);
    }
    else if (i->repeat == delay_period) {
      execute_command(i->cmd);
      i->repeat -= repeat_period;
    }
  }
}


int main(int argc, char *argv[]) 
{
  SDL_Event event;
  int error;
  char *config_file_path = NULL;
  config.exe_path = SDL_GetBasePath();

  error = handle_arguments(argc, argv, &config_file_path);
  if (error == NO_ERROR_QUIT) {
    cleanup();
    return 0;
  }
  else if (error == ERROR_QUIT) {
    cleanup();
    return 1;
  }
  output_log(LOGLEVEL_DEBUG, "Config file found: %s\n", config_file_path);

  // Parse config file for settings and menu entries
  error = ini_parse(config_file_path, config_handler, &config);
  if (error < 0) {
    output_log(LOGLEVEL_FATAL, "Fatal Error: Config file %s not found\n", config_file_path);
    cleanup();
    return 1;
  }
  free(config_file_path);

  //scan_slideshow_directory(NULL, config.slideshow_directory);
  //return 0;

  // Initialize libraries
  if (init_sdl() || init_ttf() || init_svg()) {
    cleanup();
    return 1;
  }

  // Check settings against requirements
  validate_settings();

  // Load gamepad overrides
  if (config.gamepad_enabled) {
    if (config.gamepad_mappings_file != NULL) {
      error = SDL_GameControllerAddMappingsFromFile(config.gamepad_mappings_file);
      if (error) {
        output_log(LOGLEVEL_ERROR, 
                   "Error: Could not load gamepad mappings from %s\n", 
                   config.gamepad_mappings_file);
      }
    }
  }

  // Render background image
  if (config.background_mode == MODE_IMAGE) {
    background_texture = load_texture(config.background_image,NULL);

    // Switch to color mode if loading background image failed
    if (background_texture == NULL) {
      config.background_mode = MODE_COLOR;
      output_log(LOGLEVEL_ERROR, "Error: Couldn't load background image, defaulting to color background\n");
      set_draw_color();
    }
  }

  // Initialize slideshow
  else if (config.background_mode == MODE_SLIDESHOW) {
    init_slideshow();
  }

  // Render highlight
  int button_height = config.icon_size + config.title_padding + geo.font_height;
  highlight = malloc(sizeof(highlight_t));
  highlight->rect.w = config.icon_size + 2*config.highlight_hpadding;
  highlight->rect.h = button_height + 2*config.highlight_vpadding;
  highlight->texture = render_highlight(highlight->rect.w,
                                        highlight->rect.h,
                                        config.highlight_rx);

  // Render scroll indicators
  if (config.scroll_indicators) {
    render_scroll_indicators();
  }

  // Debug info
  if (config.debug) {
    debug_settings();  
    debug_menu_entries(config.first_menu, config.num_menus);
    debug_video();
  }

  // Load the default menu and display it
  if (config.default_menu == NULL) {
    output_log(LOGLEVEL_FATAL, "Fatal Error: No default menu defined in config file\n");
    cleanup();
    exit(1);
  }

  default_menu = get_menu(config.default_menu, config.first_menu);
  if (default_menu == NULL) {
    output_log(LOGLEVEL_FATAL, 
               "Fatal Error: Default Menu \"%s\" not found in config file\n", 
               config.default_menu);
    cleanup();
    exit(1);
  }
  error = load_menu(NULL, default_menu, false, true);
  if (error) {
    cleanup();
    exit(1);
  }

  // Draw initial screen
  draw_screen();
  state.screen_updates = false;

  #ifdef _WIN32
    if (!config.debug) {
      hide_console();
    }
  #endif
  
  // Initialize timing
  ticks.main_loop = SDL_GetTicks();
  if (config.background_mode == MODE_SLIDESHOW) {
    ticks.slideshow_load = ticks.main_loop;
  }
  
  // Main program loop
  output_log(LOGLEVEL_DEBUG, "Begin program loop\n");
  while (!quit) {
    ticks.main_loop = SDL_GetTicks();
    while (SDL_PollEvent(&event)) {
      switch(event.type) {
        case SDL_QUIT:
          quit = true;
          break;

        case SDL_KEYDOWN:
          handle_keypress(&event.key.keysym);         
          break;

        case SDL_CONTROLLERDEVICEADDED:
          output_log(LOGLEVEL_DEBUG, "Gamepad connected with device index %i\n", event.cdevice.which);
          if (event.cdevice.which == config.gamepad_device) {
            connect_gamepad(event.cdevice.which);
          }
          break;

        case SDL_CONTROLLERDEVICEREMOVED:
          output_log(LOGLEVEL_DEBUG, "Gamepad removed with device index %i\n", event.cdevice.which);
          if (event.cdevice.which == config.gamepad_device) {
            SDL_GameControllerClose(gamepad);
            gamepad = NULL;
          }

        case SDL_WINDOWEVENT:
          if (event.window.event == SDL_WINDOWEVENT_SHOWN || 
          event.window.event == SDL_WINDOWEVENT_EXPOSED || 
          event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            if (config.on_launch == MODE_ON_LAUNCH_BLANK) {
              set_draw_color();
              state.screen_updates = true;
            }
            else {
              state.screen_updates = true;
            }
          }
          else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            output_log(LOGLEVEL_DEBUG, "Lost keyboard focus\n");
          }
          else if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
            output_log(LOGLEVEL_DEBUG, "Lost mouse focus\n");
          }
      }
    }
    if (gamepad != NULL) {
      poll_gamepad();
    }
    if (config.background_mode == MODE_SLIDESHOW) {
      if (!state.slideshow_transition && ticks.main_loop - ticks.slideshow_load > config.slideshow_image_duration) {
        slideshow->transition_texture = load_next_slideshow_background();
        ticks.slideshow_load = ticks.main_loop;
        state.slideshow_transition = true;
        state.screen_updates = true;

      }
      else if (state.slideshow_transition) {
        slideshow->transition_alpha += slideshow->transition_change_rate;
        if (slideshow->transition_alpha >= 255.0f) {
          SDL_SetTextureAlphaMod(slideshow->transition_texture, 0xFF);
          slideshow->transition_alpha = 0.0f;
          SDL_DestroyTexture(background_texture);
          background_texture = slideshow->transition_texture;
          SDL_SetTextureBlendMode(background_texture, SDL_BLENDMODE_BLEND);
          slideshow->transition_texture = NULL;
          state.slideshow_transition = false;
        }
        else {
          SDL_SetTextureAlphaMod(slideshow->transition_texture, (Uint8) slideshow->transition_alpha);
        }
      state.screen_updates = true;
      }
    }

    if (state.screen_updates) {
      draw_screen();
    }
    SDL_Delay(POLLING_PERIOD);
  }
  output_log(LOGLEVEL_DEBUG, "Quitting program\n");
  cleanup();

  #ifdef _WIN32
  if (!config.debug) {
    restore_console();
  }
  #endif
  return 0;
}