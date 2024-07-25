#define FPS 10
#define TILE_W 16
#define TILE_H 16
#define WORLD_W 10
#define WORLD_H 10
#define MAX_SNAKES 8
#define MAX_APPLES 4
#define TCP_PORT 8080
#define MAX_SNAKE_SIZE (WORLD_W * WORLD_H)

typedef enum {
    // Values are chosen such that -d is the direction opposite to d
    DIR_UP    = +1,
    DIR_DOWN  = -1,
    DIR_LEFT  = +2,
    DIR_RIGHT = -2,
} Direction;

typedef struct {
    bool used;
    Direction dir;
    u32 head_x;
    u32 head_y;
    u32 body_idx;
    u32 body_len;
    Direction body[MAX_SNAKE_SIZE];
    u32 iter_index;
    u32 iter_x;
    u32 iter_y;
} Snake;

typedef struct {
    bool used;
    u32 x;
    u32 y;
} Apple;

typedef struct {
    u64 frame_index;
    Snake snakes[MAX_SNAKES];
    Apple apples[MAX_APPLES];
} GameState;

typedef enum {
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_CONNECT,
} InputType;

typedef struct {
    InputType type;
} Input;

bool is_server;
int self_snake_index;
GameState oldest_game_state;
GameState latest_game_state;

TCPHandle server_handle;
TCPHandle client_handles[MAX_SNAKES-1]; // -1 because the host doesn't need one

void init_game_state(GameState *state)
{
    state->frame_index = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        state->snakes[i].used = false;
    for (int i = 0; i < MAX_APPLES; i++)
        state->apples[i].used = false;
}

void create_snake(Snake *s, u32 x, u32 y)
{
    assert(!s->used);
    s->used = true;
    s->dir = DIR_LEFT;
    s->head_x = x;
    s->head_y = y;
    s->body_idx = 0;
    s->body_len = 0;
    s->iter_index = 0;
    s->iter_x = 0;
    s->iter_y = 0;
}

void change_snake_direction(Snake *s, Direction d)
{
    // Snakes can't face the opposite direction to their movement
    if (d != -s->dir) s->dir = d;
}

void start_iter_over_snake(Snake *s)
{
    s->iter_index = 0;
}

bool next_snake_body_part(Snake *s, u32 *x, u32 *y)
{
    if (s->iter_index == 0) {
        s->iter_x = s->head_x;
        s->iter_y = s->head_y;
    } else {
        if (s->iter_index > s->body_len)
            return false;
        switch (s->body[(s->body_idx + s->iter_index) % COUNTOF(s->body)]) {
            case DIR_UP   : s->iter_y--; break;
            case DIR_DOWN : s->iter_y++; break;
            case DIR_LEFT : s->iter_x++; break;
            case DIR_RIGHT: s->iter_x--; break;
        }

        // Clamp x to [0, WORLD_W-1]
        if (s->iter_x == -1)
            s->iter_x = WORLD_W-1;
        else if (s->iter_x == WORLD_W)
            s->iter_x = 0;

        // Clamp y to [0, WORLD_H-1]
        if (s->iter_y == -1)
            s->iter_y = WORLD_H-1;
        else if (s->iter_y == WORLD_H)
            s->iter_y = 0;
    }
    s->iter_index++;
    if (x) *x = s->iter_x;
    if (y) *y = s->iter_y;
    return true;
}

bool snake_head_collided_with_someone_else(Snake *p, GameState *game)
{
    for (int i = 0; i < MAX_SNAKES; i++) {
        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        if (s == p) next_snake_body_part(s, NULL, NULL); // Don't compare the snake's head with itself
        for (u32 x, y; next_snake_body_part(s, &x, &y); )
            if (x == p->head_x && y == p->head_y)
                return true;
    }
    return false;
}

bool consume_apple_at(Apple *apples, u32 x, u32 y)
{
    int i = 0;
    while (i < MAX_APPLES) {
        if (apples[i].used && apples[i].x == x && apples[i].y == y) {
            apples[i].used = false;
            return true;
        }
        i++;
    }
    return false;
}

void move_snake_forwards(Snake *s, Apple *apples)
{
    switch (s->dir) {
        case DIR_UP   : s->head_y++; break;
        case DIR_DOWN : s->head_y--; break;
        case DIR_LEFT : s->head_x--; break;
        case DIR_RIGHT: s->head_x++; break;
    }

    // Clamp x to [0, WORLD_W-1]
    if (s->head_x == -1)
        s->head_x = WORLD_W-1;
    else if (s->head_x == WORLD_W)
        s->head_x = 0;

    // Clamp y to [0, WORLD_H-1]
    if (s->head_y == -1)
        s->head_y = WORLD_H-1;
    else if (s->head_y == WORLD_H)
        s->head_y = 0;

    s->body[s->body_idx] = s->dir;
    if (s->body_idx > 0)
        s->body_idx--;
    else
        s->body_idx = COUNTOF(s->body)-1;

    if (consume_apple_at(apples, s->head_x, s->head_y))
        s->body_len++;
}

int count_apples(Apple *apples)
{
    int n = 0;
    for (int i = 0; i < MAX_APPLES; i++)
        if (apples[i].used)
            n++;
    return n;
}

bool location_occupied_by_snake_or_apple(GameState *game, u32 x, u32 y)
{
    for (int i = 0; i < MAX_SNAKES; i++) {
        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        u32 snake_body_part_x;
        u32 snake_body_part_y;
        while (next_snake_body_part(s, &snake_body_part_x, &snake_body_part_y))
            if (snake_body_part_x == x && snake_body_part_y == y)
                return true;
    }

    for (int i = 0; i < MAX_APPLES; i++) {
        Apple *a = &game->apples[i];
        if (a->used && a->x == x && a->y == y)
            return true;
    }

    return false;
}

void choose_apple_location(GameState *game, u32 *out_x, u32 *out_y)
{
    for (int i = 0; i < 1000; i++) {
        u32 x = get_random() % WORLD_W;
        u32 y = get_random() % WORLD_H;
        if (!location_occupied_by_snake_or_apple(game, x, y)) {
            if (out_x) *out_x = x;
            if (out_y) *out_y = y;
            return;
        }
    }
    for (u32 x = 0; x < WORLD_W; x++)
        for (u32 y = 0; y < WORLD_H; y++) {
            if (!location_occupied_by_snake_or_apple(game, x, y)) {
                if (out_x) *out_x = x;
                if (out_y) *out_y = y;
                return;
            }
        }
    assert(0); // No space left
}

void make_sure_there_is_at_least_this_amount_of_apples(GameState *game, int min_apples)
{
    int num_apples = count_apples(game->apples);
    if (num_apples >= min_apples) return;

    int missing = min_apples - num_apples;
    for (int i = 0; missing > 0; i++) {
        Apple *a = &game->apples[i];
        if (!a->used) {
            choose_apple_location(game, &a->x, &a->y);
            a->used = true;
            missing--;
        }
    }
}

void update_game_state(GameState *game)
{
    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        move_snake_forwards(s, game->apples);
        if (snake_head_collided_with_someone_else(s, game))
            s->used = false; // RIP
    }

    make_sure_there_is_at_least_this_amount_of_apples(game, 2);
    game->frame_index++;
}

void draw_game_state(GameState *game)
{
    draw_rect(v2(0, 0), v2(WORLD_W*TILE_W, WORLD_H*TILE_H), COLOR_WHITE);

    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        for (u32 x, y; next_snake_body_part(s, &x, &y); )
            draw_rect(v2(x * TILE_W, y * TILE_H), v2(TILE_W, TILE_H), COLOR_GREEN);
    }

    for (int i = 0; i < MAX_APPLES; i++) {
        Apple *a = &game->apples[i];
        if (a->used)
            draw_rect(v2(a->x * TILE_W, a->y * TILE_H), v2(TILE_W, TILE_H), COLOR_RED);
    }
}

int count_snakes(GameState *game)
{
    int n = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        if (game->snakes[i].used)
            n++;
    return n;
}

void init_globals(void)
{
    server_handle = TCP_INVALID;
    for (int i = 0; i < MAX_SNAKES-1; i++)
        client_handles[i] = TCP_INVALID;
}

int entry(int argc, char **argv)
{
    init_globals();
    init_game_state(&latest_game_state);

	window.title = STR("Snake Battle Royale");
	window.scaled_width = 1280; // We need to set the scaled size if we want to handle system scaling (DPI)
	window.scaled_height = 720; 
	window.x = 200;
	window.y = 90;
	window.clear_color = hex_to_rgba(0x6495EDff);

    string font_file = STR("assets/Minecraft.ttf");
    Gfx_Font *font = load_font_from_disk(font_file, get_heap_allocator());
    if (!font) {
        printf("Couldn't load font '%s'\n", font_file);
        abort();
    }

    /*
     * Choose between client or server
     */
    int choice; // 1=server, 0=client
    while (!window.should_close) {

        draw_frame.projection = m4_make_orthographic_projection(
            window.width * -0.5, window.width * 0.5,
            window.height * -0.5, window.height * 0.5,
            -1, 10);

		reset_temporary_storage();

        static int cursor = 0;
        if (is_key_just_pressed(KEY_ARROW_UP) || is_key_just_pressed(KEY_ARROW_DOWN)) cursor = !cursor;
        if (is_key_just_pressed('A')) { choice = !cursor; break; }
        draw_text(font, STR("HOST"), 30, v2(10, 100), v2(1, 1), cursor == 0 ? COLOR_RED : COLOR_BLACK);
        draw_text(font, STR("JOIN"), 30, v2(10, 10 ), v2(1, 1), cursor == 1 ? COLOR_RED : COLOR_BLACK);

        os_update(); 
		gfx_update();
    }
    printf("%s mode\n", choice ? STR("Server") : STR("Client"));
    is_server = !!choice;

    if (is_server) {
        // Listen for connections
        server_handle = tcp_server_create(STR(""), TCP_PORT, MAX_SNAKES);
        if (server_handle == TCP_INVALID) {
            printf("Could not start server\n");
            abort();
        }

        self_snake_index = 0;
        create_snake(&latest_game_state.snakes[0], get_random() % WORLD_W, get_random() % WORLD_H);

        // Wait for other players
        bool all_players_connected = false;
        while (!all_players_connected && !window.should_close) {

            printf(".. Waiting for connections ..\n");
            tcp_server_poll(server_handle, 1000.0/FPS); // TODO: Wait until timeout
            for (;;) {
                TCPEvent event = tcp_server_event(server_handle);
                if (event.type == TCP_EVENT_NONE) break;

                if (event.type == TCP_EVENT_CONNECT) {
                    int i = 0;
                    while (client_handles[i] != TCP_INVALID) {
                        i++;
                        assert(i < MAX_SNAKES-1);
                    }
                    client_handles[i] = event.handle;
                    create_snake(&latest_game_state.snakes[i+1],
                        get_random() % WORLD_W,
                        get_random() % WORLD_H);
                    printf("Player connected\n");
                }

                if (event.type == TCP_EVENT_DISCONNECT) {
                    int i = 0;
                    while (client_handles[i] != event.handle) {
                        i++;
                        assert(i < MAX_SNAKES);
                    }
                    latest_game_state.snakes[i+1].used = false;
                    client_handles[i] = TCP_INVALID;
                    tcp_client_close(event.handle);
                    printf("Player disconnected\n");
                }

                if (event.type == TCP_EVENT_DATA) {
                    printf("Player sent unexpected data\n");
                    abort();
                }
            }

            int num_snakes = count_snakes(&latest_game_state);
            if (num_snakes == 2) all_players_connected = true;

            os_update(); 
    		gfx_update();

        }

        printf("Ready to play!\n");

        // Send player positions to the clients
        int num_snakes = count_snakes(&latest_game_state);
        for (int i = 0, j = 0; i < MAX_SNAKES-1; i++) {
            if (client_handles[i] == TCP_INVALID)
                continue;
            uint32_t buffer;

            // Send the player count
            buffer = htonl(num_snakes);
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            // Send the index associated to this client
            buffer = htonl(j); // <-- This is j and not i
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            // Send the player information
            for (int j = 0; j < MAX_SNAKES; j++) {
                Snake *s = &latest_game_state.snakes[j];
                
                buffer = htonl(s->head_x);
                tcp_client_write(client_handles[i], &buffer, sizeof(buffer));
                
                buffer = htonl(s->head_y);
                tcp_client_write(client_handles[i], &buffer, sizeof(buffer));
                
                assert(s->body_len == 0); // We are assuming the starting size is 0. If that
                                          // wasn't the case we would need to send the body
            }

            j++;
        }

    } else {
        // Connect to host
        server_handle = tcp_client_create(STR("127.0.0.1"), TCP_PORT);
        if (server_handle == TCP_INVALID) {
            printf("Couldn't connect to the server\n");
            abort();
        }
        printf("Connected!\n");

        // Receive starting state
        int num_snakes = 0;
        for (bool done = false; !done && !window.should_close; ) {

            printf(".. Waiting game state ..\n");
            tcp_client_poll(server_handle, 1000.0/FPS);

            do {
                TCPEvent event = tcp_client_event(server_handle);
                printf("Event!\n");

                if (event.type == TCP_EVENT_NONE) {
                    printf("It's none :/\n");
                    break;
                }

                if (event.type == TCP_EVENT_DISCONNECT) {
                    printf("Server disconnected unexpectedly\n");
                    abort();
                }

                assert(event.type == TCP_EVENT_DATA);
                printf("Got some data!\n");
                
                string input = tcp_client_get_input(server_handle);

                if (num_snakes == 0) {

                    if (input.count < 2 * sizeof(u32))
                        continue;
                    u32 buffer;

                    // Get snake count
                    memcpy(&buffer, input.data, sizeof(u32));
                    tcp_client_read(server_handle, sizeof(u32));
                    buffer = ntohl(buffer);
                    printf("num_snakes=%lu\n", buffer);
                    if (buffer > MAX_SNAKES) {
                        // TODO
                        abort();
                    }
                    num_snakes = (int) buffer;

                    // Get our index
                    memcpy(&buffer, input.data, sizeof(u32));
                    tcp_client_read(server_handle, sizeof(u32));
                    buffer = ntohl(buffer);
                    printf("self_snake_index=%lu\n", buffer);
                    if (buffer >= num_snakes) {
                        // TODO
                        abort();
                    }
                    self_snake_index = (int) buffer;

                }
                
                if (num_snakes > 0) {
                    printf("Got %d bytes, expected %d\n",
                        input.count, num_snakes * sizeof(u32) * 2);
                    if (input.count < num_snakes * sizeof(u32) * 2)
                        continue;
                    for (int i = 0; i < num_snakes; i++) {

                        u32 head_x;
                        u32 head_y;
            
                        // Get head position
                        memcpy(&head_x, input.data + 0, sizeof(u32));
                        memcpy(&head_y, input.data + 4, sizeof(u32));
                        tcp_client_read(server_handle, 2 * sizeof(u32));

                        head_x = ntohl(head_x);
                        head_y = ntohl(head_y);
                        printf("snake[%d].head={x=%lu, y=%lu}\n", i, head_x, head_y);
                        if (head_x >= WORLD_W || head_y >= WORLD_H) {
                            // TODO
                            printf("Invalid snake coordinates\n");
                            abort();
                        }

                        create_snake(&latest_game_state.snakes[i], head_x, head_y);
                    }
                    done = true;
                }
            } while (!done && !window.should_close);

            os_update();
    		gfx_update();
        }
    }

    memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

	while (!window.should_close) {

        float64 frame_start_time = os_get_current_time_in_seconds();

        if (is_server) {
            tcp_server_poll(server_handle, 0);
            for (;;) {
                TCPEvent event = tcp_server_event(server_handle);
                if (event.type == TCP_EVENT_NONE) break;

                // TODO
            }
        } else {
            tcp_client_poll(server_handle, 0);
            for (;;) {
                TCPEvent event = tcp_client_event(server_handle);
                if (event.type == TCP_EVENT_NONE) break;

                // TODO
            }
        }

        draw_frame.projection = m4_make_orthographic_projection(
            window.width * -0.5, window.width * 0.5,
            window.height * -0.5, window.height * 0.5,
            -1, 10);

		reset_temporary_storage();

        if (is_key_just_pressed(KEY_ARROW_UP))    change_snake_direction(&latest_game_state.snakes[self_snake_index], DIR_UP);
        if (is_key_just_pressed(KEY_ARROW_DOWN))  change_snake_direction(&latest_game_state.snakes[self_snake_index], DIR_DOWN);
        if (is_key_just_pressed(KEY_ARROW_LEFT))  change_snake_direction(&latest_game_state.snakes[self_snake_index], DIR_LEFT);
        if (is_key_just_pressed(KEY_ARROW_RIGHT)) change_snake_direction(&latest_game_state.snakes[self_snake_index], DIR_RIGHT);

        update_game_state(&latest_game_state);
        draw_game_state(&latest_game_state);

		os_update(); 
		gfx_update();

        float64 elapsed = os_get_current_time_in_seconds() - frame_start_time;
        if (elapsed < 1.0F/FPS)
            os_high_precision_sleep(1000.0F/FPS - elapsed * 1000);
	}

    destroy_font(font);

    if (is_server) {
        for (int i = 0; i < MAX_SNAKES; i++)
            if (client_handles[i] != TCP_INVALID) {
                tcp_client_close(client_handles[i]);
                client_handles[i] = TCP_INVALID;
            }
        tcp_server_delete(server_handle);
        server_handle = TCP_INVALID;
    } else {
        tcp_client_close(server_handle);
        server_handle = TCP_INVALID;
    }
	return 0;
}
