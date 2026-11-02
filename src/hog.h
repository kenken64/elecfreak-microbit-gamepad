#ifndef HOG_H_
#define HOG_H_

#include <stdbool.h>
#include <zephyr/bluetooth/conn.h>
#include "gamepad.h"

/* True once the link is encrypted and the host has enabled report notifications. */
bool hog_is_ready(struct bt_conn *conn);

/* Remember the latest report (served when the host reads the Report characteristic). */
void hog_set_report(const struct gamepad_report *report);

/* Notify the host of a new input report. */
int hog_send(struct bt_conn *conn, const struct gamepad_report *report);

#endif /* HOG_H_ */
