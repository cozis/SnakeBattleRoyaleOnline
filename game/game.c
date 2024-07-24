#define FPS 10
#define TILE_W 16
#define TILE_H 16
#define WORLD_W 10
#define WORLD_H 10
#define MAX_SNAKE_SIZE (WORLD_W * WORLD_H)
#define MAX_SNAKES 8
#define MAX_APPLES 4
#define CEIL(X, Y) (((X) + (Y) - 1) / (Y))

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

GameState oldest_game_state;
GameState latest_game_state;

#define COUNTOF(X) (sizeof(X)/sizeof((X)[0]))

void init_game_state(GameState *state)
{
    state->frame_index = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        state->snakes[i].used = false;
    for (int i = 0; i < MAX_APPLES; i++)
        state->apples[i].used = false;
}

void create_snake(u32 x, u32 y)
{
    int i = 0;
    while (i < MAX_SNAKES && latest_game_state.snakes[i].used)
        i++;
    assert(i < MAX_SNAKES);

    Snake *s = &latest_game_state.snakes[i];
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

int entry(int argc, char **argv)
{
    init_game_state(&oldest_game_state);
    init_game_state(&latest_game_state);

    create_snake(3, 3);

	window.title = STR("Snake Battle Royale");
	window.scaled_width = 1280; // We need to set the scaled size if we want to handle system scaling (DPI)
	window.scaled_height = 720; 
	window.x = 200;
	window.y = 90;
	window.clear_color = hex_to_rgba(0x6495EDff);

	while (!window.should_close) {

        float64 frame_start_time = os_get_current_time_in_seconds();

        draw_frame.projection = m4_make_orthographic_projection(
            window.width * -0.5, window.width * 0.5,
            window.height * -0.5, window.height * 0.5,
            -1, 10);

		reset_temporary_storage();

        if (is_key_just_pressed(KEY_ARROW_UP))    change_snake_direction(&latest_game_state.snakes[0], DIR_UP);
        if (is_key_just_pressed(KEY_ARROW_DOWN))  change_snake_direction(&latest_game_state.snakes[0], DIR_DOWN);
        if (is_key_just_pressed(KEY_ARROW_LEFT))  change_snake_direction(&latest_game_state.snakes[0], DIR_LEFT);
        if (is_key_just_pressed(KEY_ARROW_RIGHT)) change_snake_direction(&latest_game_state.snakes[0], DIR_RIGHT);

        update_game_state(&latest_game_state);
        draw_game_state(&latest_game_state);

		os_update(); 
		gfx_update();

        float64 elapsed = os_get_current_time_in_seconds() - frame_start_time;
        if (elapsed < 1.0F/FPS)
            os_high_precision_sleep(1000.0F/FPS - elapsed * 1000);
	}

	return 0;
}
