#include <stdio.h>

#include "./level_picker.h"

#include "game/sprite_font.h"
#include "system/lt.h"
#include "system/nth_alloc.h"
#include "system/stacktrace.h"
#include "system/str.h"
#include "system/log.h"
#include "system/file.h"

#define TITLE_MARGIN_TOP 100.0f
#define TITLE_MARGIN_BOTTOM 100.0f

#define LEVEL_PICKER_LIST_FONT_SCALE vec(5.0f, 5.0f)
#define LEVEL_PICKER_LIST_PADDING_BOTTOM 50.0f

void level_picker_populate(LevelPicker *level_picker,
                           const char *dirpath)
{
    trace_assert(level_picker);
    trace_assert(dirpath);

    level_picker->background.base_color = hexstr("073642");
    level_picker->camera_position = vec(0.0f, 0.0f);

    {
        dynarray_clear(&level_picker->items);
        level_picker->items = create_dynarray(METADATA_FILEPATH_MAX_SIZE);

        DIR *level_dir = opendir(dirpath);
        if (level_dir == NULL) {
            log_fail("Can't open asset folder: %s\n", dirpath);
            abort();
        }

        char filepath[METADATA_FILEPATH_MAX_SIZE];
        for (struct dirent *d = readdir(level_dir);
             d != NULL;
             d = readdir(level_dir)) {
            if (*d->d_name == '.') continue;

            snprintf(filepath, METADATA_FILEPATH_MAX_SIZE,
                     "%s/%s", dirpath, d->d_name);
            dynarray_push(&level_picker->items, filepath);
        }
        closedir(level_dir);
    }

    level_picker->wiggly_text = (WigglyText) {
        .text = "Select Level",
        .scale = {10.0f, 10.0f},
        .color = COLOR_WHITE,
    };
}

int level_picker_render(const LevelPicker *level_picker,
                        const Camera *camera)
{
    trace_assert(level_picker);

    const Rect viewport = camera_view_port_screen(camera);

    if (background_render(&level_picker->background, camera) < 0) {
        return -1;
    }

    const Vec2f title_size = wiggly_text_size(&level_picker->wiggly_text);

    wiggly_text_render(
        &level_picker->wiggly_text,
        camera,
        vec(viewport.w * 0.5f - title_size.x * 0.5f, TITLE_MARGIN_TOP));

    for (size_t i = 0; i < level_picker->items.count; ++i) {
        const Vec2f current_position = vec_sum(
            level_picker->position,
            vec(0.0f, (float) i * ((float) FONT_CHAR_HEIGHT * LEVEL_PICKER_LIST_FONT_SCALE.y + LEVEL_PICKER_LIST_PADDING_BOTTOM)));

        const char *item_text = dynarray_pointer_at(&level_picker->items, i);

        sprite_font_render_text(
            &camera->font,
            camera->renderer,
            current_position,
            LEVEL_PICKER_LIST_FONT_SCALE,
            rgba(1.0f, 1.0f, 1.0f, 1.0f),
            item_text);

        if (i == level_picker->cursor) {
            SDL_Rect boundary_box = rect_for_sdl(
                sprite_font_boundary_box(
                    current_position,
                    LEVEL_PICKER_LIST_FONT_SCALE,
                    item_text));
            if (SDL_SetRenderDrawColor(camera->renderer, 255, 255, 255, 255) < 0) {
                return -1;
            }

            if (SDL_RenderDrawRect(camera->renderer, &boundary_box) < 0) {
                return -1;
            }
        }
    }

    {
        /* CSS */
        const float padding = 20.0f;
        const Vec2f size = vec(3.0f, 3.0f);
        const Vec2f position = vec(0.0f, viewport.h - size.y * FONT_CHAR_HEIGHT);

        /* HTML */
        camera_render_text_screen(
            camera,
            "Press 'N' to create new level",
            size,
            COLOR_WHITE,
            vec(position.x + padding,
                position.y - padding));
    }

    return 0;
}

int level_picker_update(LevelPicker *level_picker,
                        float delta_time)
{
    trace_assert(level_picker);

    vec_add(&level_picker->camera_position,
            vec(50.0f * delta_time, 0.0f));

    if (wiggly_text_update(&level_picker->wiggly_text, delta_time) < 0) {
        return -1;
    }

    return 0;
}

static
Vec2f level_picker_list_size(const LevelPicker *level_picker)
{
    trace_assert(level_picker);

    Vec2f result = vec(0.0f, 0.0f);

    for (size_t i = 0; i < level_picker->items.count; ++i) {
        const char *item_text = dynarray_pointer_at(
            &level_picker->items,
            i);

        Rect boundary_box = sprite_font_boundary_box(
            vec(0.0f, 0.0f),
            LEVEL_PICKER_LIST_FONT_SCALE,
            item_text);

        result.x = fmaxf(result.x, boundary_box.w);
        result.y += boundary_box.y + LEVEL_PICKER_LIST_PADDING_BOTTOM;
    }

    return result;
}


int level_picker_event(LevelPicker *level_picker,
                       const SDL_Event *event)
{
    trace_assert(level_picker);
    trace_assert(event);

    switch (event->type) {
    case SDL_WINDOWEVENT: {
        switch (event->window.event) {
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
            int width;
            SDL_GetRendererOutputSize(SDL_GetRenderer(SDL_GetWindowFromID(event->window.windowID)), &width, NULL);
            const Vec2f title_size = wiggly_text_size(&level_picker->wiggly_text);
            const Vec2f selector_size = level_picker_list_size(level_picker);

            level_picker->position =
                vec((float)width * 0.5f - selector_size.x * 0.5f,
                    TITLE_MARGIN_TOP + title_size.y + TITLE_MARGIN_BOTTOM);
        } break;
        }
    } break;

    case SDL_KEYDOWN:
        switch (event->key.keysym.sym) {
        case SDLK_UP:
            if (level_picker->cursor == 0) {
                level_picker->cursor = level_picker->items.count - 1;
            } else {
                level_picker->cursor--;
            }
            break;
        case SDLK_DOWN:
            level_picker->cursor++;
            if (level_picker->cursor == level_picker->items.count) {
                level_picker->cursor = 0;
            }
            break;
        case SDLK_RETURN:
            if (level_picker->cursor < level_picker->items.count) {
                level_picker->selected_item = (int) level_picker->cursor;
            }
            break;
        }
        break;

    case SDL_MOUSEMOTION: {
        const Vec2f mouse_pos = vec((float) event->motion.x, (float) event->motion.y);
        Vec2f position = level_picker->position;

        for (size_t i = 0; i < level_picker->items.count; ++i) {
            const char *item_text = dynarray_pointer_at(
                &level_picker->items,
                i);

            Rect boundary_box = sprite_font_boundary_box(
                position,
                LEVEL_PICKER_LIST_FONT_SCALE,
                item_text);

            if (rect_contains_point(boundary_box, mouse_pos)) {
                level_picker->cursor = i;
            }

            position.y += boundary_box.h + LEVEL_PICKER_LIST_PADDING_BOTTOM;
        }
    } break;

    case SDL_MOUSEBUTTONDOWN: {
        switch (event->button.button) {
        case SDL_BUTTON_LEFT: {
            // check if the click position was actually inside...
            // note: make sure there's actually stuff in the list! tsoding likes
            // to remove all levels and change title to "SMOL BREAK"...
            if (level_picker->items.count == 0)
                break;

            // note: this assumes that all list items are the same height!
            // this is probably a valid assumption as long as we use a sprite font.
            float single_item_height =
                FONT_CHAR_HEIGHT * LEVEL_PICKER_LIST_FONT_SCALE.y + LEVEL_PICKER_LIST_PADDING_BOTTOM;

            Vec2f position = level_picker->position;
            vec_add(&position, vec(0.0f, (float) level_picker->cursor * single_item_height));

            const char *item_text =
                dynarray_pointer_at(
                    &level_picker->items,
                    level_picker->cursor);

            Rect boundary_box = sprite_font_boundary_box(
                position,
                LEVEL_PICKER_LIST_FONT_SCALE,
                item_text);

            const Vec2f mouse_pos = vec((float) event->motion.x, (float) event->motion.y);
            if (rect_contains_point(boundary_box, mouse_pos)) {
                level_picker->selected_item = (int) level_picker->cursor;
            }
        } break;
        }
    } break;
    }

    return 0;
}

int level_picker_input(LevelPicker *level_picker,
                       const Uint8 *const keyboard_state,
                       SDL_Joystick *the_stick_of_joy)
{
    trace_assert(level_picker);
    trace_assert(keyboard_state);
    (void) the_stick_of_joy;
    return 0;
}

const char *level_picker_selected_level(const LevelPicker *level_picker)
{
    trace_assert(level_picker);

    if (level_picker->selected_item < 0) {
        return NULL;
    }

    return dynarray_pointer_at(
        &level_picker->items,
        (size_t)level_picker->selected_item);
}

void level_picker_clean_selection(LevelPicker *level_picker)
{
    trace_assert(level_picker);
    level_picker->selected_item = -1;
}

int level_picker_enter_camera_event(LevelPicker *level_picker,
                                    Camera *camera)
{
    camera_center_at(camera, level_picker->camera_position);
    return 0;
}
