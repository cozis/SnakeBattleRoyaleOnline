#define SPRITE_W 8
#define SPRITE_H 8

typedef struct {
    bool used;
    Direction dir, next_dir;
    u32 head_x;
    u32 head_y;
    u32 body_idx;
    u32 body_len;
    Direction body[MAX_SNAKE_SIZE];

    // Iterator state is embedded in the snake
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
    u64 seed;
    bool apple_consumed_this_frame;
    Snake snakes[MAX_SNAKES];
    Apple apples[MAX_APPLES];
} GameState;

Gfx_Image *sprite_sheet;

/*
 * FIXME: The way we avoid snakes changing direction to the one opposite
 *        to movement is to check that the new direction is not.. opposite
 *        to the movement. The problem is, players can change direction
 *        multiple times per frame with only the last one taking effect.
 *        This makes it possible to change from the current direction to
 *        and ortogonal one and then to the opposite direction to the initial
 *        one. This should not happen.
 */

int count_snakes(GameState *game)
{
    int n = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        if (game->snakes[i].used)
            n++;
    return n;
}

void init_game_state(GameState *state)
{
    state->seed = 1;
    state->frame_index = 0;
    for (int i = 0; i < MAX_SNAKES; i++) state->snakes[i].used = false;
    for (int i = 0; i < MAX_APPLES; i++) state->apples[i].used = false;
}

Snake *find_unused_snake_slot(GameState *game)
{
    for (int i = 0; i < MAX_SNAKES; i++) {
        Snake *s = &game->snakes[i];
        if (!s->used) return s;
    }
    return NULL;
}

u64 get_random_from_game(GameState *game)
{
    game->seed = next_random(game->seed);
    return game->seed;
}

void init_snake(Snake *s, u32 x, u32 y)
{
    assert(s && !s->used);
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

void spawn_snake(GameState *game)
{
    Snake *s = find_unused_snake_slot(game);
    assert(s && !s->used);

    u32 x = get_random_from_game(game) % WORLD_W;
    u32 y = get_random_from_game(game) % WORLD_H;

    init_snake(s, x, y);
}

void change_snake_direction(Snake *s, Direction d)
{
    // Snakes can't face the opposite direction to their movement
    if (d != -s->dir) s->next_dir = d;
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

bool move_snake_forwards(Snake *s, Apple *apples)
{
    s->dir = s->next_dir;

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

    if (consume_apple_at(apples, s->head_x, s->head_y)) {
        s->body_len++;
        return true;
    } else {
        return false;
    }
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

bool choose_apple_location(GameState *game, u32 *out_x, u32 *out_y)
{
    for (int i = 0; i < 1000; i++) {
        u32 x = get_random_from_game(game) % WORLD_W;
        u32 y = get_random_from_game(game) % WORLD_H;
        if (!location_occupied_by_snake_or_apple(game, x, y)) {
            printf("Apple location (%d, %d)\n", x, y);
            if (out_x) *out_x = x;
            if (out_y) *out_y = y;
            return true;
        }
    }
    for (u32 x = 0; x < WORLD_W; x++)
        for (u32 y = 0; y < WORLD_H; y++) {
            if (!location_occupied_by_snake_or_apple(game, x, y)) {
                if (out_x) *out_x = x;
                if (out_y) *out_y = y;
                return true;
            }
        }
    return false;
}

void make_sure_there_is_at_least_this_amount_of_apples(GameState *game, int min_apples)
{
    int num_apples = count_apples(game->apples);
    if (num_apples >= min_apples) return;

    int missing = min_apples - num_apples;
    for (int i = 0; i < MAX_APPLES && missing > 0; i++) {

        Apple *a = &game->apples[i];

        if (!a->used) {
            if (choose_apple_location(game, &a->x, &a->y)) {
                a->used = true;
                missing--;
            }
        }
    }
}

void apply_input_to_game_instance(GameState *game, Input input)
{
    assert(game->snakes[input.player].used == true);

    if (input.disconnect) {
        game->snakes[input.player].used = false;
    } else {
        switch (input.dir) {
            case DIR_UP   : change_snake_direction(&game->snakes[input.player], DIR_UP);    break;
            case DIR_DOWN : change_snake_direction(&game->snakes[input.player], DIR_DOWN);  break;
            case DIR_LEFT : change_snake_direction(&game->snakes[input.player], DIR_LEFT);  break;
            case DIR_RIGHT: change_snake_direction(&game->snakes[input.player], DIR_RIGHT); break;
        }
    }
}

void update_game_instance(GameState *game)
{
    game->apple_consumed_this_frame = false;

    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        bool grow = move_snake_forwards(s, game->apples);
        if (grow) game->apple_consumed_this_frame = true;

        if (snake_head_collided_with_someone_else(s, game))
            s->used = false; // RIP
    }

    make_sure_there_is_at_least_this_amount_of_apples(game, 4);
    game->frame_index++;
}

void draw_snake(Snake *s, float offset_x, float offset_y, float scale)
{
    //Direction prev_dir;
    start_iter_over_snake(s);
    for (u32 i = 0, x, y; next_snake_body_part(s, &x, &y); i++) {
        if (i == 0) {

            /*
             * Draw head
             */

            int rotate;
            switch (s->dir) {
                case DIR_UP   : rotate = 1; break;
                case DIR_DOWN : rotate = 3; break;
                case DIR_LEFT : rotate = 0; break;
                case DIR_RIGHT: rotate = 2; break;
            }
            draw_subimage(sprite_sheet, rotate,
                offset_x + x * scale * TILE_W,
                offset_y + y * scale * TILE_H,
                scale * TILE_W,
                scale * TILE_H,
                1 * SPRITE_W,
                1 * SPRITE_H,
                SPRITE_W, SPRITE_H);

        } else if (i == s->body_len) {

            /*
             * Draw tail
             */

            int sprite_x = 0;
            int sprite_y = 1;
            int rotate = 0;
            switch (s->body[(s->body_idx + i) % MAX_SNAKE_SIZE]) {
                case DIR_UP   : rotate = 1; break;
                case DIR_DOWN : rotate = 3; break;
                case DIR_LEFT : rotate = 0; break;
                case DIR_RIGHT: rotate = 2; break;
            }

            draw_subimage(sprite_sheet, rotate,
                offset_x + x * scale * TILE_W,
                offset_y + y * scale * TILE_H,
                scale * TILE_W,
                scale * TILE_H,
                sprite_x * SPRITE_W,
                sprite_y * SPRITE_H,
                SPRITE_W, SPRITE_H);

        } else {

            /*
             * Draw body
             */

            int sprite_x = 0;
            int sprite_y = 1;
            int rotate = 0;

            Direction curr_dir = s->body[(s->body_idx + i + 0) % MAX_SNAKE_SIZE];
            Direction next_dir = s->body[(s->body_idx + i + 1) % MAX_SNAKE_SIZE];

            #define PAIR(X, Y) (((u64) (u32) (X) << 32) | (u64) (u32) (Y))
            switch (PAIR(curr_dir, next_dir)) {
                case PAIR(DIR_UP,    DIR_UP   ): sprite_x = 0; sprite_y = 0; rotate = 1; break;
                case PAIR(DIR_DOWN,  DIR_DOWN ): sprite_x = 0; sprite_y = 0; rotate = 1; break;
                case PAIR(DIR_LEFT,  DIR_LEFT ): sprite_x = 0; sprite_y = 0; rotate = 0; break;
                case PAIR(DIR_RIGHT, DIR_RIGHT): sprite_x = 0; sprite_y = 0; rotate = 0; break;
                case PAIR(DIR_UP,    DIR_LEFT ): sprite_x = 1; sprite_y = 0; rotate = 2; break;
                case PAIR(DIR_LEFT,  DIR_UP   ): sprite_x = 1; sprite_y = 0; rotate = 0; break;
                case PAIR(DIR_UP,    DIR_RIGHT): sprite_x = 1; sprite_y = 0; rotate = 1; break;
                case PAIR(DIR_RIGHT, DIR_UP   ): sprite_x = 1; sprite_y = 0; rotate = 3; break;
                case PAIR(DIR_DOWN,  DIR_LEFT ): sprite_x = 1; sprite_y = 0; rotate = 3; break;
                case PAIR(DIR_LEFT,  DIR_DOWN ): sprite_x = 1; sprite_y = 0; rotate = 1; break;
                case PAIR(DIR_DOWN,  DIR_RIGHT): sprite_x = 1; sprite_y = 0; rotate = 0; break;
                case PAIR(DIR_RIGHT, DIR_DOWN ): sprite_x = 1; sprite_y = 0; rotate = 2; break;
                default: assert(0); break;
            }

            draw_subimage(sprite_sheet, rotate,
                offset_x + x * scale * TILE_W,
                offset_y + y * scale * TILE_H,
                scale * TILE_W,
                scale * TILE_H,
                sprite_x * SPRITE_W,
                sprite_y * SPRITE_H,
                SPRITE_W, SPRITE_H);
        }
    }
}

void draw_game_instance(GameState *game)
{
    float pad_x = 50;
    float pad_y = 20;

    float nopad_window_w = window.width  - 2 * pad_x;
    float nopad_window_h = window.height - 2 * pad_y;

    float scale;
    {
        float scale_x = nopad_window_w / (WORLD_W * TILE_W);
        float scale_y = nopad_window_h / (WORLD_H * TILE_H);
        scale = MIN(scale_x, scale_y);
    }

    float px_world_w = scale * WORLD_W * TILE_W;
    float px_world_h = scale * WORLD_H * TILE_H;

    // Offsets needed to center stuff
    float offset_x = (window.width  - px_world_w) / 2;
    float offset_y = (window.height - px_world_h) / 2;

    draw_rect(v2(offset_x, offset_y), v2(px_world_w, px_world_h), COLOR_WHITE);

    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        draw_snake(s, offset_x, offset_y, scale);
    }

    for (int i = 0; i < MAX_APPLES; i++) {
        Apple *a = &game->apples[i];
        if (a->used)
            draw_rect(v2(offset_x + a->x * scale * TILE_W, offset_y + a->y * scale * TILE_H), v2(scale * TILE_W, scale * TILE_H), COLOR_RED);
    }
}
