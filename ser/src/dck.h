#ifndef DCK_H
#define DCK_H

/* TODO:
 * [ ] Allocators.
 */

#define dck_stretchy_t(data_type, size_type) struct { data_type *data; size_type count, capacity; }

/* argument must be an 'lvalue', except for `item` */
#define dck_stretchy_push(dck, item)                                                    \
do {                                                                                    \
    if ((dck).count == (dck).capacity) {                                                \
        (dck).capacity = (dck).capacity ? (dck).capacity * 2                            \
                                          : 4096 / sizeof(*((dck).data));               \
        (dck).data = realloc((dck).data, sizeof(*((dck).data)) * (dck).capacity);       \
        malloc_check((dck).data);                                                       \
    }                                                                                   \
    (dck).data[(dck).count] = item;                                                     \
    (dck).count++;                                                                      \
} while (0)

/* argument must be an 'lvalue', except for `amount` */
#define dck_stretchy_reserve(dck, amount)                                               \
do {                                                                                    \
    if ((dck).count + (amount) > (dck).capacity) {                                      \
        if ((dck).capacity == 0) {                                                      \
            (dck).capacity = 4096 / sizeof(*((dck).data));                              \
        }                                                                               \
        while ((dck).count + (amount) > (dck).capacity) {                               \
            (dck).capacity *= 2;                                                        \
        }                                                                               \
        (dck).data = realloc((dck).data, sizeof(*((dck).data)) * (dck).capacity);       \
        malloc_check((dck).data);                                                       \
    }                                                                                   \
} while (0)


#endif // DCK_H
