
Gfx_Font *font;

Rect target_menu_box;
Rect menu_box;

int self_snake_index;

typedef enum {
    VIEW_MAIN_MENU,
    VIEW_WAITING_FOR_PLAYERS,
    VIEW_CONNECTING,
    VIEW_PLAY,
    VIEW_COULDNT_CONNECT,
    VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY,
} ViewID;

ViewID current_view = VIEW_MAIN_MENU;

u32 get_current_player_id(void)
{
    return self_snake_index;
}

void main_menu_loop(void)
{
    typedef struct {
        string text;
        Rect   rect;
    } MainMenuEntry;

    static MainMenuEntry entries[] = {
        {.text=LIT("PLAY")},
        {.text=LIT("HOST")},
        {.text=LIT("JOIN")},
        {.text=LIT("EXIT")},
    };

    static int cursor = 0;

    int pad_y = 30;
    int text_h = 40;

    Gfx_Text_Metrics metrics;

    for (int i = 0; i < COUNTOF(entries); i++) {
        Gfx_Text_Metrics metrics = measure_text(font, entries[i].text, text_h, v2(1, 1));
        entries[i].rect.w = metrics.visual_size.x;
        entries[i].rect.h = metrics.visual_size.y;
    }

    float total_h = (COUNTOF(entries) - 1) * pad_y;
    for (int i = 0; i < COUNTOF(entries); i++)
        total_h += entries[i].rect.h;
    
    for (int i = 0; i < COUNTOF(entries); i++) {
        entries[i].rect.x = (window.width - entries[i].rect.w) / 2;
        entries[i].rect.y = (window.height - total_h) / 2 + (COUNTOF(entries) - i - 1) * (text_h + pad_y);
    }

    bool submit = false;

    for (int i = 0; i < COUNTOF(entries); i++) {
        if (mouse_in_rect(padded_rect(entries[i].rect, 10))) {
            if (cursor != i) {
                play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
                cursor = i;
            }
            submit = submit || is_key_just_pressed(MOUSE_BUTTON_LEFT);
            break;
        }
    }

    if (is_key_just_pressed(KEY_ARROW_UP)) {
        if (cursor > 0) {
            cursor--;
            play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
        }
    }

    if (is_key_just_pressed(KEY_ARROW_DOWN)) {
        if (cursor < COUNTOF(entries)-1) {
            cursor++;
            play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
        }
    }

    target_menu_box = padded_rect(entries[cursor].rect, 10);

    if (is_key_just_pressed(KEY_SPACEBAR))
        submit = true;

    if (submit) {
        switch (cursor) {
            case 0:
            input_queue_init();
            init_game_state(&latest_game_state);
            is_server = true;
            multiplayer = false;
            self_snake_index = 0;
            spawn_snake(&latest_game_state);
            current_view = VIEW_PLAY;
            break;
            case 1:
            input_queue_init();
            if (start_waiting_for_players(1)) {
                is_server = true;
                multiplayer = true;
                current_view = VIEW_WAITING_FOR_PLAYERS;
            } else {
                abort();
                // TODO: An error occurred
            }
            break;
            case 2:
            input_queue_init();
            is_server = false;
            multiplayer = true;
            current_view = VIEW_CONNECTING;
            start_connecting();
            break;
            case 3:
            window.should_close = true;
            break;
        }
        play_one_audio_clip(STR("assets/sounds/mixkit-player-jumping-in-a-video-game-2043.wav"));
    }

    for (int i = 0; i < COUNTOF(entries); i++)
        draw_text(font, entries[i].text, text_h, v2(entries[i].rect.x, entries[i].rect.y), v2(1, 1), cursor == i ? COLOR_RED : COLOR_BLACK);
    
    draw_rect_border(menu_box, 3, COLOR_BLACK);
}

void message_and_button_loop(string msg, string btn, ViewID view)
{
    int pad_y = 30;
    int text_h = 40;

    Gfx_Text_Metrics metrics;
    
    Rect msg_rect;
    Rect btn_rect;

    metrics = measure_text(font, msg, text_h, v2(1, 1));
    msg_rect.w = metrics.visual_size.x;
    msg_rect.h = metrics.visual_size.y;

    metrics = measure_text(font, btn, text_h, v2(1, 1));
    btn_rect.w = metrics.visual_size.x;
    btn_rect.h = metrics.visual_size.y;

    float total_h = msg_rect.h + btn_rect.h + pad_y;

    msg_rect.x = (window.width  - msg_rect.w) / 2;
    msg_rect.y = (window.height - total_h) / 2 + pad_y + text_h;

    btn_rect.x = (window.width  - btn_rect.w) / 2;
    btn_rect.y = (window.height - total_h) / 2;

    draw_text(font, msg, text_h, v2(msg_rect.x, msg_rect.y), v2(1, 1), COLOR_BLACK);
    draw_text(font, btn, text_h, v2(btn_rect.x, btn_rect.y), v2(1, 1), COLOR_RED);

    if (is_key_just_pressed(KEY_SPACEBAR)) {
        current_view = view;
        play_one_audio_clip(STR("assets/sounds/mixkit-player-jumping-in-a-video-game-2043.wav"));
    }

    if (mouse_in_rect(padded_rect(btn_rect, 10))) {
        if (is_key_just_pressed(MOUSE_BUTTON_LEFT)) {
            current_view = view;
            play_one_audio_clip(STR("assets/sounds/mixkit-player-jumping-in-a-video-game-2043.wav"));
        }
    }

    target_menu_box = padded_rect(btn_rect, 10);

    draw_rect_border(menu_box, 3, COLOR_BLACK);
}

void wait_for_players_loop(void)
{
    if (wait_for_players()) {

        /*
         * All players connected
         */

        init_game_state(&latest_game_state);
        int num_players = 1 + count_client_handles();
        for (int i = 0; i < num_players; i++)
            spawn_snake(&latest_game_state);
        memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

        self_snake_index = 0;

        // Send player positions to clients
        for (int i = 0, j = 0; i < MAX_CLIENTS; i++) {

            if (client_handles[i] == TCP_INVALID) {
                continue;
            } else {
                j++;
            }

            u64 time = htonll(get_absolute_time_us());
            tcp_client_write(client_handles[i], &time, sizeof(time));

            u64 seed = htonll(latest_game_state.seed);
            tcp_client_write(client_handles[i], &seed, sizeof(seed));

            // Send the player count
            u32 buffer = htonl(num_players);
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            // Send the index associated to this client
            buffer = htonl(j); // <-- This is j and not i
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            // Send the player information
            for (int k = 0; k < MAX_SNAKES; k++) {
                Snake *s = &latest_game_state.snakes[k];
                if (!s->used) continue;

                buffer = htonl(s->head_x);
                tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

                buffer = htonl(s->head_y);
                tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

                assert(s->body_len == 0); // We are assuming the starting size is 0. If that
                                          // wasn't the case we would need to send the body
            }

            j++;
        }

        current_view = VIEW_PLAY;

    } else {

        message_and_button_loop(STR("Waiting for players"), STR("CANCEL"), VIEW_MAIN_MENU);
        if (current_view != VIEW_WAITING_FOR_PLAYERS) {
            stop_waiting_for_players();
            network_reset();
        }
    }
}

void connecting_loop(void)
{
    message_and_button_loop(STR("Connecting..."), STR("CANCEL"), VIEW_MAIN_MENU);
    if (current_view == VIEW_MAIN_MENU) {
        stop_connecting();
        network_reset();
        return;
    }

    if (server_handle != TCP_INVALID) {

        InitialGameStateMessage initial;
        int result = poll_for_initial_state(&initial);
        switch (result) {

            case -1:
            // Server disconnected
            // TODO
            current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
            network_reset();
            break;

            case 1:
            {
                /*
                 * State received
                 */

                init_game_state(&latest_game_state);
                for (int i = 0; i < initial.num_snakes; i++)
                    init_snake(&latest_game_state.snakes[i],
                               initial.snakes[i].head_x,
                               initial.snakes[i].head_y);
                latest_game_state.seed = initial.seed;
                memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

                u64 current_time_us = get_absolute_time_us();
                if (current_time_us > initial.time_us) {
                    u32 latency_frames = (float) (current_time_us - initial.time_us) * FPS / 1000000;
                    latest_game_state.frame_index = latency_frames;
                    printf("latency_frames=%d\n", latency_frames);
                }

                self_snake_index = (int) initial.self_index;
                current_view = VIEW_PLAY;
            }
            break;

            case 0: /* No message */ break;
        }

    } else if (done_connecting()) {

        if (server_handle == TCP_INVALID) {
            current_view = VIEW_COULDNT_CONNECT;
            network_reset();
            return;
        }

        init_game_state(&latest_game_state);
        memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));
    }
}

void play_loop(void)
{
    poll_for_inputs();
    for (Input input; get_input(&input); ) {
        apply_input_to_game(input);
        if (input.player == 0 && input.disconnect) {
            current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
            network_reset();
            return;
        }
    }

    update_game();
    draw_game();

    if (game_apple_consumed_this_frame()) {
        //play_one_audio_clip(STR("assets/sounds/mixkit-winning-a-coin-video-game-2069.wav"));
    }

    if (game_complete()) {
        current_view = VIEW_MAIN_MENU;
        network_reset();
    }
}

int entry(int argc, char **argv)
{
	window.title = STR("Snake Battle Royale");
	window.scaled_width = 500; // We need to set the scaled size if we want to handle system scaling (DPI)
	window.scaled_height = 500;
	window.x = 200;
	window.y = 90;
	window.clear_color = hex_to_rgba(0x6495EDff);

    network_init();

    string font_file = STR("assets/Minecraft.ttf");
    font = load_font_from_disk(font_file, get_heap_allocator());
    if (!font) {
        printf("Couldn't load font '%s'\n", font_file);
        abort();
    }

    string sprite_sheet_file = STR("assets/sprites/snake.png");
    sprite_sheet = load_image_from_disk(sprite_sheet_file, get_heap_allocator());
    if (!sprite_sheet) {
        printf("Couldn't load image '%s'\n", sprite_sheet_file);
        abort();
    }

    while (!window.should_close) {

        float64 frame_start_time = os_get_current_time_in_seconds();

        reset_temporary_storage();
        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        switch (current_view) {
            
            case VIEW_MAIN_MENU:
            main_menu_loop();
            break;
            
            case VIEW_WAITING_FOR_PLAYERS:
            wait_for_players_loop();
            break;
            
            case VIEW_CONNECTING:
            connecting_loop();
            break;
            
            case VIEW_PLAY:
            play_loop();
            break;

            case VIEW_COULDNT_CONNECT:
            message_and_button_loop(STR("Couldn't connect to host"), STR("MAIN MENU"), VIEW_MAIN_MENU);
            break;

            case VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY:
            message_and_button_loop(STR("Server disconnected unexpectedly"), STR("MAIN MENU"), VIEW_MAIN_MENU);
            break;
        }

        os_update();
		gfx_update();

        float64 elapsed = os_get_current_time_in_seconds() - frame_start_time;
        animate_rect_to_target(&menu_box, target_menu_box, elapsed, 40);
    }

    network_free();
    destroy_font(font);
	return 0;
}
