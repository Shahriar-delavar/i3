/*
 * vim:ts=4:sw=4:expandtab
 */

#include "all.h"
#include "cmdparse.tab.h"

typedef enum { BEFORE, AFTER } position_t;

/*
 * This function detaches 'con' from its parent and inserts it either before or
 * after 'target'.
 *
 */
static void insert_con_into(Con *con, Con *target, position_t position) {
    Con *parent = target->parent;
    /* We need to preserve the old con->parent. While it might still be used to
     * insert the entry before/after it, we call the on_remove_child callback
     * afterwards which might then close the con if it is empty. */
    Con *old_parent = con->parent;

    con_detach(con);
    con_fix_percent(con->parent);

    con->parent = parent;

    if (position == BEFORE) {
        TAILQ_INSERT_BEFORE(target, con, nodes);
        TAILQ_INSERT_HEAD(&(parent->focus_head), con, focused);
    } else if (position == AFTER) {
        TAILQ_INSERT_AFTER(&(parent->nodes_head), target, con, nodes);
        TAILQ_INSERT_HEAD(&(parent->focus_head), con, focused);
    }

    /* Pretend the con was just opened with regards to size percent values.
     * Since the con is moved to a completely different con, the old value
     * does not make sense anyways. */
    con->percent = 0.0;
    con_fix_percent(parent);

    CALL(old_parent, on_remove_child);
}

/*
 * This function detaches 'con' from its parent and inserts it at the given
 * workspace.
 *
 */
static void attach_to_workspace(Con *con, Con *ws) {
    con_detach(con);
    con_fix_percent(con->parent);

    CALL(con->parent, on_remove_child);

    con->parent = ws;

    TAILQ_INSERT_TAIL(&(ws->nodes_head), con, nodes);
    TAILQ_INSERT_TAIL(&(ws->focus_head), con, focused);

    /* Pretend the con was just opened with regards to size percent values.
     * Since the con is moved to a completely different con, the old value
     * does not make sense anyways. */
    con->percent = 0.0;
    con_fix_percent(ws);
}

/*
 * Moves the current container in the given direction (TOK_LEFT, TOK_RIGHT,
 * TOK_UP, TOK_DOWN from cmdparse.l)
 *
 */
void tree_move(int direction) {
    DLOG("Moving in direction %d\n", direction);
    /* 1: get the first parent with the same orientation */
    Con *con = focused;

    if (con->type == CT_WORKSPACE) {
        DLOG("Not moving workspace\n");
        return;
    }

    if (con->parent->type == CT_WORKSPACE && con_num_children(con->parent) == 1) {
        DLOG("This is the only con on this workspace, not doing anything\n");
        return;
    }

    orientation_t o = (direction == TOK_LEFT || direction == TOK_RIGHT ? HORIZ : VERT);

    Con *same_orientation = con_parent_with_orientation(con, o);
    /* There is no parent container with the same orientation */
    if (!same_orientation) {
        if (con_is_floating(con)) {
            /* this is a floating con, we just disable floating */
            floating_disable(con, true);
            return;
        }
        if (con_inside_floating(con)) {
            /* 'con' should be moved out of a floating container */
            DLOG("Inside floating, moving to workspace\n");
            attach_to_workspace(con, con_get_workspace(con));
            goto end;
        }
        DLOG("Force-changing orientation\n");
        ws_force_orientation(con_get_workspace(con), o);
        same_orientation = con_parent_with_orientation(con, o);
    }

    /* easy case: the move is within this container */
    if (same_orientation == con->parent) {
        DLOG("We are in the same container\n");
        Con *swap;
        /* TODO: TAILQ_SWAP? */
        if (direction == TOK_LEFT || direction == TOK_UP) {
            if (!(swap = TAILQ_PREV(con, nodes_head, nodes)))
                return;

            if (!con_is_leaf(swap)) {
                insert_con_into(con, con_descend_focused(swap), AFTER);
                goto end;
            }

            /* the container right of the current one is a normal one. */
            con_detach(con);
            TAILQ_INSERT_BEFORE(swap, con, nodes);
            TAILQ_INSERT_HEAD(&(swap->parent->focus_head), con, focused);
        } else {
            if (!(swap = TAILQ_NEXT(con, nodes)))
                return;

            if (!con_is_leaf(swap)) {
                insert_con_into(con, con_descend_focused(swap), AFTER);
                goto end;
            }

            con_detach(con);
            TAILQ_INSERT_AFTER(&(swap->parent->nodes_head), swap, con, nodes);
            TAILQ_INSERT_HEAD(&(swap->parent->focus_head), con, focused);
        }
        DLOG("Swapped.\n");
        return;
    }

    /* this time, we have to move to another container */
    /* This is the container *above* 'con' which is inside 'same_orientation' */
    Con *above = con;
    while (above->parent != same_orientation)
        above = above->parent;

    DLOG("above = %p\n", above);
    Con *next;
    position_t position;
    if (direction == TOK_UP || direction == TOK_LEFT) {
        position = BEFORE;
        next = TAILQ_PREV(above, nodes_head, nodes);
    } else if (direction == TOK_DOWN || direction == TOK_RIGHT) {
        position = AFTER;
        next = TAILQ_NEXT(above, nodes);
    }

    /* special case: there is a split container in the direction we are moving
     * to, so descend and append */
    if (next && !con_is_leaf(next))
        insert_con_into(con, con_descend_focused(next), AFTER);
    else
        insert_con_into(con, above, position);

end:
    /* We need to call con_focus() to fix the focus stack "above" the container
     * we just inserted the focused container into (otherwise, the parent
     * container(s) would still point to the old container(s)). */
    con_focus(con);

    tree_flatten(croot);
}
