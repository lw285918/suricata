/* Copyright (C) 2024 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Shivani Bhardwaj <shivani@oisf.net>
 */

#include "util-interval-tree.h"
#include "util-validate.h"
#include "detect-engine-siggroup.h"
#include "detect-engine-port.h"

/**
 * \brief Function to compare two interval nodes. This defines the order
 *        of insertion of a node in the interval tree. This also updates
 *        the max attribute of any node in a given tree if needed.
 *
 * \param a First node to compare
 * \param b Second node to compare
 *
 * \return 1 if low of node a is bigger, -1 otherwise
 */
static int PICompareAndUpdate(const SCIntervalNode *a, SCIntervalNode *b)
{
    if (a->port2 > b->max) {
        b->max = a->port2;
    }
    if (a->port >= b->port) {
        SCReturnInt(1);
    }
    SCReturnInt(-1);
}

// cppcheck-suppress nullPointerRedundantCheck
IRB_GENERATE(PI, SCIntervalNode, irb, PICompareAndUpdate);

/**
 * \brief Function to initialize the interval tree.
 *
 * \return Pointer to the newly created interval tree
 */
SCIntervalTree *SCIntervalTreeInit(void)
{
    SCIntervalTree *it = SCCalloc(1, sizeof(SCIntervalTree));
    if (it == NULL) {
        return NULL;
    }

    return it;
}

/**
 * \brief Helper function to free a given node in the interval tree.
 *
 * \param de_ctx Detection Engine Context
 * \param it Pointer to the interval tree
 */
static void SCIntervalNodeFree(DetectEngineCtx *de_ctx, SCIntervalTree *it)
{
    SCIntervalNode *node = NULL, *safe = NULL;
    IRB_FOREACH_SAFE(node, PI, &it->tree, safe)
    {
        SigGroupHeadFree(de_ctx, node->sh);
        PI_IRB_REMOVE(&it->tree, node);
        SCFree(node);
    }
    it->head = NULL;
}

/**
 * \brief Function to free an entire interval tree.
 *
 * \param de_ctx Detection Engine Context
 * \param it Pointer to the interval tree
 */
void SCIntervalTreeFree(DetectEngineCtx *de_ctx, SCIntervalTree *it)
{
    if (it) {
        SCIntervalNodeFree(de_ctx, it);
        SCFree(it);
    }
}

/**
 * \brief Function to insert a node in the interval tree.
 *
 * \param de_ctx Detection Engine Context
 * \param it Pointer to the interval tree
 * \param p Pointer to a DetectPort object
 *
 * \return SC_OK if the node was inserted successfully, SC_EINVAL otherwise
 */
int PIInsertPort(DetectEngineCtx *de_ctx, SCIntervalTree *it, const DetectPort *p)
{
    DEBUG_VALIDATE_BUG_ON(p->port > p->port2);

    SCIntervalNode *pi = SCCalloc(1, sizeof(*pi));
    if (pi == NULL) {
        return SC_EINVAL;
    }

    pi->port = p->port;
    pi->port2 = p->port2;
    SigGroupHeadCopySigs(de_ctx, p->sh, &pi->sh);

    if (PI_IRB_INSERT(&it->tree, pi) != NULL) {
        SCLogDebug("Node wasn't added to the tree: port: %d, port2: %d", pi->port, pi->port2);
        SCFree(pi);
        return SC_EINVAL;
    }
    return SC_OK;
}

/**
 * \brief Function to check if a port range overlaps with a given set of ports
 *
 * \param port Given low port
 * \param port2 Given high port
 * \param ptr Pointer to the node in the tree to be checked against
 *
 * \return true if an overlaps was found, false otherwise
 */
static bool IsOverlap(const uint16_t port, const uint16_t port2, const SCIntervalNode *ptr)
{
    /* Two intervals i and i' are said to overlap if
     * - i (intersection) i' != NIL
     * - i.low <= i'.high
     * - i'.low <= i.high
     *
     * There are four possible cases of overlaps as shown below which
     * are all covered by the if condition that follows.
     *
     * Case 1:         [.........] i
     *            [...................] i'
     *
     * Case 2:    [...................] i
     *                 [.........] i'
     *
     * Case 3:               [........] i
     *            [..............] i'
     *
     * Case 4:    [..............] i
     *                  [.............] i'
     */
    if (port <= ptr->port2 && ptr->port <= port2) {
        return true;
    }

    SCLogDebug("No overlap found for [%d, %d] w [%d, %d]", port, port2, ptr->port, ptr->port2);
    return false;
}

/**
 * \brief Function to find all the overlaps of given ports with the existing
 *        port ranges in the interval tree. This function takes in a low and
 *        a high port, considers it a continuos range and tries to match it
 *        against all the given port ranges in the interval tree. This search
 *        for overlap happens in min(O(k*log(n)), O(n*n)) time where,
 *        n = number of nodes in the tree, and,
 *        k = number of intervals with which an overlap was found
 *
 * \param de_ctx Detection Engine Context
 * \param port Given low port
 * \param port2 Given high port
 * \param ptr Pointer to the root of the tree
 * \param list A list of DetectPort objects to be filled
 */
static void FindOverlaps(DetectEngineCtx *de_ctx, const uint16_t port, const uint16_t port2,
        SCIntervalNode *root, DetectPort **list)
{
    DetectPort *new_port = NULL;
    int stack_depth = 0;
    SCIntervalNode *stack[100], *current = root;
    memset(&stack, 0, sizeof(stack));

    while (current || stack_depth) {
        while (current != NULL) {
            if (current->max < port) {
                current = NULL;
                break;
            }
            const bool is_overlap = IsOverlap(port, port2, current);

            if (is_overlap && (new_port == NULL)) {
                /* Allocate memory for port obj only if it's first overlap */
                new_port = DetectPortInit();
                if (new_port == NULL) {
                    goto error;
                }

                SCLogDebug("Found overlaps for [%u:%u], creating new port", port, port2);
                new_port->port = port;
                new_port->port2 = port2;
                SigGroupHeadCopySigs(de_ctx, current->sh, &new_port->sh);

                /* Since it is guaranteed that the ports received by this stage
                 * will be sorted, insert any new ports to the end of the list
                 * and avoid walking the entire list */
                if (*list == NULL) {
                    *list = new_port;
                    (*list)->last = new_port;
                } else if (((*list)->last->port != new_port->port) &&
                           ((*list)->last->port2 != new_port->port)) {
                    DEBUG_VALIDATE_BUG_ON(new_port->port < (*list)->last->port);
                    (*list)->last->next = new_port;
                    (*list)->last = new_port;
                } else {
                    SCLogDebug("Port already exists in the list");
                    goto error;
                }
            } else if (is_overlap && (new_port != NULL)) {
                SCLogDebug("Found overlaps for [%u:%u], adding sigs", port, port2);
                /* Only copy the relevant SGHs on later overlaps */
                SigGroupHeadCopySigs(de_ctx, current->sh, &new_port->sh);
            }

            stack[stack_depth++] = current;
            current = IRB_LEFT(current, irb);
        }

        if (stack_depth == 0) {
            SCLogDebug("Stack depth was exhausted");
            break;
        }

        SCIntervalNode *popped = stack[stack_depth - 1];
        stack_depth--;
        BUG_ON(popped == NULL);
        current = IRB_RIGHT(popped, irb);
    }
    return;
error:
    if (new_port != NULL)
        DetectPortFree(de_ctx, new_port);
    return;
}

/**
 * \brief Callee function to find all overlapping port ranges as asked
 *        by the detection engine during Stage 2 of signature grouping.
 *
 * \param de_ctx Detection Engine Context
 * \param port Given low port
 * \param port2 Given high port
 * \param head Pointer to the head of the tree named PI
 * \param list Pointer to the list of port objects that needs to be filled/updated
 */
void PISearchOverlappingPortRanges(DetectEngineCtx *de_ctx, const uint16_t port,
        const uint16_t port2, const struct PI *head, DetectPort **list)
{
    if (head == NULL) {
        SCLogDebug("Tree head should not be NULL. Nothing to do further.");
        return;
    }
    SCIntervalNode *ptr = IRB_ROOT(head);
    SCLogDebug("Finding overlaps for the range [%d, %d]", port, port2);
    FindOverlaps(de_ctx, port, port2, ptr, list);
}
