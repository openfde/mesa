/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"

#include "util/u_dynarray.h"

struct barrier {
   nir_cf_node *node;
   nir_def *bar;
};

struct add_barriers_state {
   const struct nak_compiler *nak;
   nir_builder builder;
   struct util_dynarray barriers;
   bool progress;
};

static void
add_bar_cf_node(nir_cf_node *node, struct add_barriers_state *state)
{
   nir_builder *b = &state->builder;

   nir_block *before = nir_cf_node_as_block(nir_cf_node_prev(node));
   nir_block *after = nir_cf_node_as_block(nir_cf_node_next(node));

   b->cursor = nir_after_block(before);
   nir_def *bar = nir_bar_set_nv(b);

   b->cursor = nir_before_block_after_phis(after);
   nir_bar_sync_nv(b, bar);

   struct barrier barrier = {
      .node = node,
      .bar = bar,
   };
   util_dynarray_append(&state->barriers, struct barrier, barrier);

   state->progress = true;
}

static void
break_loop_bars(nir_block *block, struct add_barriers_state *state)
{
   if (exec_list_is_empty(&block->instr_list))
      return;

   nir_instr *block_last_instr = nir_block_last_instr(block);
   if (block_last_instr->type != nir_instr_type_jump)
      return;

   nir_jump_instr *jump = nir_instr_as_jump(block_last_instr);
   assert(jump->type == nir_jump_break ||
          jump->type == nir_jump_continue);

   nir_builder *b = &state->builder;
   b->cursor = nir_before_instr(&jump->instr);

   const unsigned num_bars =
      util_dynarray_num_elements(&state->barriers, struct barrier);

   int idx = (int)num_bars - 1;
   for (nir_cf_node *p = block->cf_node.parent;
        p->type != nir_cf_node_loop; p = p->parent) {
      if (idx < 0)
         break;

      const struct barrier *bar =
         util_dynarray_element(&state->barriers, struct barrier, idx);
      if (bar->node == p) {
         nir_bar_break_nv(b, bar->bar);
         idx--;
      }
   }
}

static void
pop_bar_cf_node(nir_cf_node *node, struct add_barriers_state *state)
{
   if (state->barriers.size == 0)
      return;

   const struct barrier *top =
      util_dynarray_top_ptr(&state->barriers, struct barrier);
   if (top->node == node)
      (void)util_dynarray_pop(&state->barriers, struct barrier);
}

/* Checks if this CF node's immediate successor has a sync.  There's no point
 * in adding a sync if the very next thing we do, besides dealing with phis,
 * is to sync.
 */
static bool
cf_node_imm_succ_is_sync(nir_cf_node *node)
{
   nir_block *block = nir_cf_node_as_block(nir_cf_node_next(node));

   nir_foreach_instr(instr, block) {
      if (instr->type == nir_instr_type_phi)
         continue;

      if (instr->type != nir_instr_type_intrinsic)
         return false;

      nir_intrinsic_instr *sync = nir_instr_as_intrinsic(instr);
      if (sync->intrinsic == nir_intrinsic_bar_sync_nv)
         return true;

      return false;
   }

   /* We can only get here if the only thing in the block is phis */

   /* There's another loop or if following and we didn't find a sync */
   if (nir_cf_node_next(&block->cf_node))
      return false;

   /* No successor in the CF list. Check the parent. */
   nir_cf_node *parent = block->cf_node.parent;
   switch (parent->type) {
   case nir_cf_node_if:
      return cf_node_imm_succ_is_sync(parent);
   case nir_cf_node_loop:
      /* We want to sync before the continue to avoid additional divergence.
       * We could possibly avoid the sync in the case where the loop is known
       * to be divergent and therefore syncs afterwards but this seems safer
       * for now.
       *
       * Note that this also catches double loops where you have something
       * like this:
       *
       *    loop {
       *       ...
       *       if (div) {
       *          loop {
       *             ...
       *          }
       *       }
       *    }
       *
       * In this case, we'll place a merge after the if and so we don't need a
       * merge around the inner loop.
       */
      return false;
   case nir_cf_node_function:
      /* The end of the function is a natural sync point */
      return true;
   default:
      unreachable("Unknown CF node type");
   }
}

static void
add_barriers_cf_list(struct exec_list *cf_list,
                     struct add_barriers_state *state)
{
   foreach_list_typed(nir_cf_node, node, node, cf_list) {
      switch (node->type) {
      case nir_cf_node_block:
         break_loop_bars(nir_cf_node_as_block(node), state);
         break;
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);

         if (nif->condition.ssa->divergent &&
             !cf_node_imm_succ_is_sync(&nif->cf_node))
            add_bar_cf_node(&nif->cf_node, state);

         add_barriers_cf_list(&nif->then_list, state);
         add_barriers_cf_list(&nif->else_list, state);

         pop_bar_cf_node(&nif->cf_node, state);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);

         if (loop->divergent && !cf_node_imm_succ_is_sync(&loop->cf_node))
            add_bar_cf_node(&loop->cf_node, state);

         add_barriers_cf_list(&loop->body, state);

         pop_bar_cf_node(&loop->cf_node, state);
         break;
      }
      default:
         unreachable("Unknown CF node type");
      }
   }
}

static bool
nak_nir_add_barriers_impl(nir_function_impl *impl,
                          const struct nak_compiler *nak)
{
   struct add_barriers_state state = {
      .nak = nak,
      .builder = nir_builder_create(impl),
   };
   util_dynarray_init(&state.barriers, NULL);

   add_barriers_cf_list(&impl->body, &state);

   util_dynarray_fini(&state.barriers);

   if (state.progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance |
                                  nir_metadata_loop_analysis);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return state.progress;
}

bool
nak_nir_add_barriers(nir_shader *nir, const struct nak_compiler *nak)
{
   if (nak->sm < 75) {
      nir_shader_preserve_all_metadata(nir);
      return false;
   }

   bool progress = false;

   nir_foreach_function_impl(impl, nir)
      progress |= nak_nir_add_barriers_impl(impl, nak);

   return progress;
}