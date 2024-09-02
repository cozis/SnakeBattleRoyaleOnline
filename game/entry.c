
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
	VIEW_GAME_SETUP,
	VIEW_CREATING_LOBBY,
	VIEW_COULDNT_CREATE_LOBBY,
	VIEW_LOBBY_LIST,
	VIEW_LOADING_LOBBY_LIST,
	VIEW_COULDNT_LOAD_LOBBY_LIST,
	VIEW_JOINING_LOBBY,
	VIEW_COULDNT_JOIN_LOBBY,
	VIEW_BUILD_DOESNT_SUPPORT_MULTIPLAYER,
	VIEW_YOU_WIN,
	VIEW_YOU_LOSE,
	VIEW_DRAW,
} ViewID;

ViewID current_view = VIEW_MAIN_MENU;

u32 get_current_player_id(void)
{
    return self_snake_index;
}

void message_and_button_loop(string msg, string btn, ViewID view);

Rect calc_rect_for_horizontally_centered_text(string text, float text_h, float y)
{
	Rect rect;
	rect.h = text_h;

	Gfx_Text_Metrics metrics = measure_text(font, text, rect.h, v2(1, 1));
	rect.w = metrics.visual_size.x;

	rect.x = (window.width - rect.w) / 2;
	rect.y = window.height - rect.h - y;

	return rect;
}

Rect draw_horizontally_centered_text(string text, float text_h, float y)
{
	Rect rect = calc_rect_for_horizontally_centered_text(text, text_h, y);
	draw_text(font, text, rect.h, v2(rect.x, rect.y), v2(1, 1), COLOR_BLACK);
	return rect;
}

void draw_separator(float w_percent, float h, float y)
{
	float w = window.width * w_percent;
	Vector2 p0 = {
		.x = (window.width - w) / 2,
		.y = window.height - y,
	};
	Vector2 p1 = {
		.x = p0.x + w,
		.y = p0.y,
	};
	draw_line(p0, p1, h, COLOR_BLACK);
}

void draw_rect_2(Rect rect, Vector4 color)
{
	draw_rect(v2(rect.x, rect.y), v2(rect.w, rect.h), color);
}

typedef struct {
	string text;
	Rect   rect;
} MenuEntry;

bool draw_menu(MenuEntry *entries, int num_entries, int *cursor)
{
	int pad_y = 30;
    int text_h = 40;

    Gfx_Text_Metrics metrics;

    for (int i = 0; i < num_entries; i++) {
        Gfx_Text_Metrics metrics = measure_text(font, entries[i].text, text_h, v2(1, 1));
        entries[i].rect.w = metrics.visual_size.x;
        entries[i].rect.h = metrics.visual_size.y;
    }

    float total_h = (num_entries - 1) * pad_y;
    for (int i = 0; i < num_entries; i++)
        total_h += entries[i].rect.h;
    
    for (int i = 0; i < num_entries; i++) {
        entries[i].rect.x = (window.width - entries[i].rect.w) / 2;
        entries[i].rect.y = (window.height - total_h) / 2 + (num_entries - i - 1) * (text_h + pad_y);
    }

    bool submit = false;

    for (int i = 0; i < num_entries; i++) {
        if (mouse_in_rect(padded_rect(entries[i].rect, 10))) {
            if (*cursor != i) {
                play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
                *cursor = i;
            }
            submit = submit || is_key_just_pressed(MOUSE_BUTTON_LEFT);
            break;
        }
    }

    if (is_key_just_pressed(KEY_ARROW_UP)) {
        if (*cursor > 0) {
            (*cursor)--;
            play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
        }
    }

    if (is_key_just_pressed(KEY_ARROW_DOWN)) {
        if (*cursor < num_entries-1) {
            (*cursor)++;
            play_one_audio_clip(STR("assets/sounds/mixkit-game-ball-tap-2073.wav"));
        }
    }

    target_menu_box = padded_rect(entries[*cursor].rect, 10);

    if (is_key_just_pressed(KEY_SPACEBAR))
        submit = true;
	
	for (int i = 0; i < num_entries; i++)
        draw_text(font, entries[i].text, text_h, v2(entries[i].rect.x, entries[i].rect.y), v2(1, 1), *cursor == i ? COLOR_RED : COLOR_BLACK);
    
    draw_rect_border(menu_box, 3, COLOR_BLACK);

	if (submit)
		play_one_audio_clip(STR("assets/sounds/mixkit-player-jumping-in-a-video-game-2043.wav"));

	return submit;
}

#if HAVE_MULTIPLAYER

int num_players__ = -1;

void game_setup_loop(void)
{
	MenuEntry entries[] = {
		{.text=STR("2 PLAYERS")},
		{.text=STR("3 PLAYERS")},
		{.text=STR("4 PLAYERS")},
	};
	static int cursor = 0;
	if (draw_menu(entries, COUNTOF(entries), &cursor)) {

		int num_players = cursor+2;
		num_players__ = num_players;

		steam_create_lobby_start(num_players);
		current_view = VIEW_CREATING_LOBBY;
	}
}

void create_lobby_loop(void)
{
	steam_update();

	switch (steam_create_lobby_result()) {

		case 0:
		message_and_button_loop(STR("Creating Lobby"), STR("CANCEL"), VIEW_MAIN_MENU);
		if (current_view != VIEW_CREATING_LOBBY) {
			// TODO: Cleanup
		}
		break;

		case 1:
		{
			input_queue_init();
			if (!start_waiting_for_players(num_players__-1)) {
				abort();
				// TODO: An error occurred
			}

			//steam_invite_to_lobby();

			is_server = true;
			multiplayer = true;
			current_view = VIEW_WAITING_FOR_PLAYERS;
		}
		break;

		case -1:
		current_view = VIEW_COULDNT_CREATE_LOBBY;
		break;
	}
}

void wait_for_players_loop(void)
{
	net_update();

    if (wait_for_players()) {

        /*
         * All players connected
         */

        send_initial_state();
        current_view = VIEW_PLAY;

    } else {

        message_and_button_loop(STR("Waiting for players"), STR("CANCEL"), VIEW_MAIN_MENU);
        if (current_view != VIEW_WAITING_FOR_PLAYERS) {
			printf("User canceled waiting for players\n");
            stop_waiting_for_players();
            net_reset();
        }
    }
}

void connecting_loop(void)
{
	net_update();

	switch (net_connect_status()) {

		case CONNECT_PENDING:
		message_and_button_loop(STR("Connecting..."), STR("CANCEL"), VIEW_MAIN_MENU);
		if (current_view == VIEW_MAIN_MENU) {
			printf("User canceled connection to server\n");
			net_connect_stop();
			net_reset();
			return;
		}
		break;

		case CONNECT_FAILED:
		current_view = VIEW_COULDNT_CONNECT;
		printf("Connection failed\n");
		net_reset();
		return;

		case CONNECT_OK:
		{
			InitialGameStateMessage initial;
			int result = poll_for_initial_state(&initial);
			switch (result) {

				case -1:
				// Server disconnected
				// TODO
				current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
				printf("Server disconnected unexpectedly\n");
				net_reset();
				break;

				case 1:
				{
					/*
					* State received
					*/
					start_client_game(&initial);
					current_view = VIEW_PLAY;
				}
				break;

				case 0:
				/* No message */
				message_and_button_loop(STR("Downloading Initial State..."), STR("CANCEL"), VIEW_MAIN_MENU);
				if (current_view == VIEW_MAIN_MENU) {
					net_connect_stop();
					printf("User canceled initial state download\n");
					net_reset();
					return;
				}
				break;
			}
		}
		break;
	}
}

void load_lobby_list_loop(void)
{
	net_update();

	switch (steam_lobby_list_status()) {
		case 0:
		message_and_button_loop(STR("LOADING LOBBY LIST"), STR("MAIN MENU"), VIEW_MAIN_MENU);
		break;

		case 1:
		current_view = VIEW_LOBBY_LIST;
		break;

		case -1:
		current_view = VIEW_COULDNT_LOAD_LOBBY_LIST;
		break;
	}
}

void list_lobbies_loop(void)
{
	Rect r;
	float y = 10;

	r = draw_horizontally_centered_text(STR("FRIEND'S LOBBY LIST"), 30, y);
	y += r.h;

	y += 10;
	draw_separator(0.6, 3, y);
	y += 10;

	int count = steam_lobby_list_count();
	//printf("lobby count %d\n", count);

	MenuEntry entries[128];
	uint64_t lobby_ids[128];

	if (count > COUNTOF(entries)) {
		printf("Buffer too small\n");
		abort();
	}

	for (int i = 0; i < count; i++) {
		uint64_t lobby_id = steam_get_lobby(i);
		entries[i].text = STR(steam_get_lobby_title(lobby_id));
		entries[i].rect = (Rect) {0, 0, 0, 0};
		lobby_ids[i] = lobby_id;
	}

	static int cursor = 0;
	if (draw_menu(entries, count, &cursor)) {
		uint64_t lobby_id = lobby_ids[cursor];
		steam_join_lobby_start(lobby_id);

		current_view = VIEW_JOINING_LOBBY;
	}
}

void joining_lobby_loop(void)
{
	switch (steam_join_lobby_status()) {
		case 0:
		message_and_button_loop(STR("Joining Lobby"), STR("Cancel"), VIEW_MAIN_MENU);
		// TODO: Cleanup
		break;

		case 1:
		{
			uint64_t peer_id = steam_current_lobby_owner();
			if (net_connect_start(peer_id)) {
				input_queue_init();
				is_server = false;
				multiplayer = true;
				current_view = VIEW_CONNECTING;
			} else {
				// TODO
				abort();
			}
		}
		break;

		case -1:
		current_view = VIEW_COULDNT_JOIN_LOBBY;
		break;
	}
}
#endif /* HAVE_MULTIPLAYER */


void main_menu_loop(void)
{
    static MenuEntry entries[] = {
        {.text=LIT("PLAY")},
        {.text=LIT("HOST")},
        {.text=LIT("JOIN")},
        {.text=LIT("EXIT")},
    };

    static int cursor = 0;

    if (draw_menu(entries, COUNTOF(entries), &cursor)) {
        switch (cursor) {
            case 0: /* PLAY */
            input_queue_init();
            init_game_state(&latest_game_state);
            is_server = true;
            multiplayer = false;
            self_snake_index = 0;
            spawn_snake(&latest_game_state);
            current_view = VIEW_PLAY;
            break;
            case 1: /* HOST */
#if HAVE_MULTIPLAYER
			current_view = VIEW_GAME_SETUP;
#else
			current_view = VIEW_BUILD_DOESNT_SUPPORT_MULTIPLAYER;
#endif
            break;
            case 2: /* JOIN */
#if HAVE_MULTIPLAYER
			steam_list_lobbies_owned_by_friends();
			current_view = VIEW_LOADING_LOBBY_LIST;
#else
			current_view = VIEW_BUILD_DOESNT_SUPPORT_MULTIPLAYER;
#endif
            break;
            case 3: /* EXIT */
            window.should_close = true;
            break;
        }
    }
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

u64 get_target_frame_index();
u64 last_frame_index_received_from_server = -1;

static float game_complete_time = -1;

void play_loop(void)
{
    poll_for_inputs();

	Input input;
	SyncMessage sync = {.empty=true};

    while (get_local_input(&input)) {
        apply_input_to_game(input);
#if HAVE_MULTIPLAYER
        if (input.player == 0 && input.disconnect) {
            current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
			printf("Server disconnected unexpectedly while reading for inputs\n");
            net_reset();
            return;
        }
#endif
    }

#if HAVE_MULTIPLAYER
	if (multiplayer) {
		if (is_server) {
			while (get_client_input_from_network(&input)) {
				printf("Client input is from %lld frames ago\n", (s64) get_current_frame_index() - (s64) input.time);
				apply_input_to_game(input);
				if (input.player == 0 && input.disconnect) {
					current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
					printf("Server disconnected unexpectedly while reading for inputs\n");
					net_reset();
					return;
				}
			}
		} else {
			while (get_server_input_from_network(&input, &sync)) {
				
				if (input.player == 0) {
					printf("Server input is from %lld frames ago (relative to target) and %lld frames ago relative to current\n", (s64) get_target_frame_index() - (s64) input.time, (s64) get_current_frame_index() - (s64) input.time);
					last_frame_index_received_from_server = input.time;
				}
				apply_input_to_game(input);
				if (input.player == 0 && input.disconnect) {
					current_view = VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY;
					printf("Server disconnected unexpectedly while reading for inputs\n");
					net_reset();
					return;
				}
			}
		}
	}
#endif

    update_game(sync);
    draw_game();

    if (game_apple_consumed_this_frame()) {
        //play_one_audio_clip(STR("assets/sounds/mixkit-winning-a-coin-video-game-2069.wav"));
    }

    if (game_complete()) {
		float current_time = os_get_current_time_in_seconds();
		if (game_complete_time < 0)
			game_complete_time = current_time;
		else {
			float elapsed_since_complete = current_time - game_complete_time;
			if (elapsed_since_complete > 1) {
				switch (game_result()) {
					case GAME_RESULT_WIN: current_view = VIEW_YOU_WIN;   break;
					case GAME_RESULT_LOSE: current_view = VIEW_YOU_LOSE; break;
					case GAME_RESULT_DRAW: current_view = VIEW_DRAW;     break;
				}
#if HAVE_MULTIPLAYER
		        net_reset();
#endif
			}
		}
    } else {
		game_complete_time = -1;
	}
}

void prelude(void)
{
#if HAVE_MULTIPLAYER
	net_init();
#endif
}

int entry(int argc, char **argv)
{
	window.title = STR("Snake Battle Royale");
	window.scaled_width = 500; // We need to set the scaled size if we want to handle system scaling (DPI)
	window.scaled_height = 500;
	window.x = 200;
	window.y = 90;
	window.clear_color = hex_to_rgba(0x6495EDff);

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

	float last_frame_time = 1;

    while (!window.should_close) {

        float64 frame_start_time = os_get_current_time_in_seconds();

        reset_temporary_storage();
        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        switch (current_view) {

            case VIEW_MAIN_MENU:
            main_menu_loop();
            break;
            
            case VIEW_PLAY:
            play_loop();
            break;

			case VIEW_BUILD_DOESNT_SUPPORT_MULTIPLAYER:
			message_and_button_loop(STR("This build doesn't support multiplayer"), STR("MAIN MENU"), VIEW_MAIN_MENU);
			break;

#if HAVE_MULTIPLAYER

            case VIEW_WAITING_FOR_PLAYERS:
            wait_for_players_loop();
            break;

            case VIEW_CONNECTING:
            connecting_loop();
            break;

            case VIEW_COULDNT_CONNECT:
            message_and_button_loop(STR("Couldn't connect to host"), STR("MAIN MENU"), VIEW_MAIN_MENU);
            break;

            case VIEW_SERVER_DISCONNECTED_UNEXPECTEDLY:
            message_and_button_loop(STR("Server disconnected unexpectedly"), STR("MAIN MENU"), VIEW_MAIN_MENU);
            break;

			case VIEW_CREATING_LOBBY:
			create_lobby_loop();
            break;

			case VIEW_COULDNT_CREATE_LOBBY:
			message_and_button_loop(STR("Couldn't create lobby"), STR("MAIN MENU"), VIEW_MAIN_MENU);
			break;

			case VIEW_GAME_SETUP:
			game_setup_loop();
			break;

			case VIEW_LOADING_LOBBY_LIST:
			load_lobby_list_loop();
			break;

			case VIEW_LOBBY_LIST:
			list_lobbies_loop();
			break;

			case VIEW_COULDNT_LOAD_LOBBY_LIST:
			message_and_button_loop(STR("Couldn't load lobby list"), STR("MAIN MENU"), VIEW_MAIN_MENU);
			break;

			case VIEW_JOINING_LOBBY:
			joining_lobby_loop();
			break;

			case VIEW_COULDNT_JOIN_LOBBY:
			message_and_button_loop(STR("Couldn't join lobby"), STR("Main Menu"), VIEW_MAIN_MENU);
			break;

#endif /* HAVE_MULTIPLAYER*/

			case VIEW_YOU_WIN:
			message_and_button_loop(STR("YOU WIN"), STR("Main Menu"), VIEW_MAIN_MENU);
			break;

			case VIEW_YOU_LOSE:
			message_and_button_loop(STR("YOU LOSE"), STR("Main Menu"), VIEW_MAIN_MENU);
			break;

			case VIEW_DRAW:
			message_and_button_loop(STR("DRAW"), STR("Main Menu"), VIEW_MAIN_MENU);
			break;

			default:
			abort();
			break;
        }

		{
			string text = tprint("FPS %2.2f", 1/last_frame_time);
			draw_horizontally_centered_text(text, 24, 0);
		}

        os_update();
		gfx_update();

        float64 elapsed = os_get_current_time_in_seconds() - frame_start_time;
        animate_rect_to_target(&menu_box, target_menu_box, elapsed, 40);

		last_frame_time = elapsed;
    }

#if HAVE_MULTIPLAYER
    net_free();
#endif

    destroy_font(font);
	return 0;
}
