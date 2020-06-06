/*++
Copyright (c) 2017 Microsoft Corporation and Arie Gurfinkel

Module Name:

    spacer_generalizers.cpp

Abstract:

    Lemma generalizers.

Author:

    Nikolaj Bjorner (nbjorner) 2011-11-20.
    Arie Gurfinkel

Revision History:

--*/


#include "muz/spacer/spacer_context.h"
#include "muz/spacer/spacer_generalizers.h"
#include "ast/ast_util.h"
#include "ast/expr_abstract.h"
#include "ast/rewriter/var_subst.h"
#include "ast/for_each_expr.h"
#include "ast/rewriter/factor_equivs.h"
#include "ast/rewriter/expr_safe_replace.h"
#include "ast/substitution/matcher.h"
#include "ast/expr_functors.h"
#include "smt/smt_solver.h"
#include "qe/qe_term_graph.h"

namespace spacer {
void lemma_sanity_checker::operator()(lemma_ref &lemma) {
    unsigned uses_level;
    expr_ref_vector cube(lemma->get_ast_manager());
    cube.append(lemma->get_cube());
    ENSURE(lemma->get_pob()->pt().check_inductive(lemma->level(),
                                                  cube, uses_level,
                                                  lemma->weakness()));
}

namespace{
    class contains_array_op_proc : public i_expr_pred {
        ast_manager &m;
        family_id m_array_fid;
    public:
        contains_array_op_proc(ast_manager &manager) :
            m(manager), m_array_fid(m.mk_family_id("array"))
            {}
        bool operator()(expr *e) override {
            return is_app(e) && to_app(e)->get_family_id() == m_array_fid;
        }
    };
}

// ------------------------
// lemma_bool_inductive_generalizer
/// Inductive generalization by dropping and expanding literals
void lemma_bool_inductive_generalizer::operator()(lemma_ref &lemma) {
    if (lemma->get_cube().empty()) return;
    TRACE("spacer.ind_gen", tout<<"LEMMA:\n"<<mk_and(lemma->get_cube())<<"\n";);
    // STRACE("spacer.ind_gen", tout<<"POB:\n"<<lemma->get_pob()<<"\n";);       

    // STRACE("spacer.ind_gen", tout<<"USE LIT EXPANSION?\n"<<m_use_expansion<<"\n";);
    m_st.count++;
    scoped_watch _w_(m_st.watch);

    unsigned uses_level;
    pred_transformer &pt = lemma->get_pob()->pt();
    ast_manager &m = pt.get_ast_manager();

    contains_array_op_proc has_array_op(m);
    check_pred has_arrays(has_array_op, m);

    expr_ref_vector cube(m);
    cube.append(lemma->get_cube());

    bool dirty = false;
    expr_ref true_expr(m.mk_true(), m);
    ptr_vector<expr> processed;
    expr_ref_vector extra_lits(m);

    unsigned weakness = lemma->weakness();

    unsigned i = 0, num_failures = 0;

    //FOR DEBUGGING ONLY
    // pt.check_inductive(lemma->level(), cube, uses_level, weakness, true);
    while (i < cube.size() &&
           (!m_failure_limit || num_failures < m_failure_limit)) {
        std::time_t start = std::time(nullptr);
        expr_ref lit(m);
        lit = cube.get(i);
        
        if (m_array_only && !has_arrays(lit)) {
            processed.push_back(lit);
            ++i;
            continue;
        }
        // STRACE("spacer.ind_gen", tout<<"CUBE:\n"<<mk_and(cube)<<"\n";);       
        // STRACE("spacer.ind_gen", tout<<"trying to drop \n:"<<lit<<"\n";);

        cube[i] = true_expr;

        if (cube.size() > 1 &&
            pt.check_inductive(lemma->level(), cube, uses_level, weakness)) {
            std::time_t after_check_ind = std::time(nullptr);
            // TRACE("spacer.ind_gen", tout<<"\tpassed check_ind in:"<<after_check_ind - start <<"\n";);
            num_failures = 0;
            dirty = true;
            for (i = 0; i < cube.size() &&
                     processed.contains(cube.get(i)); ++i);
        } else {
            std::time_t after_check_ind = std::time(nullptr);
            // TRACE("spacer.ind_gen", tout<<"\tfailed check_ind in:"<<after_check_ind - start <<"\n";);
             // check if the literal can be expanded and any single bb
            // literal in the expansion can replace it

            if(m_use_expansion){
                extra_lits.reset();
                extra_lits.push_back(lit);
                expand_literals(m, extra_lits);
                SASSERT(extra_lits.size() > 0);
                bool found = false;
                if (extra_lits.get(0) != lit && extra_lits.size() > 1) {
                    for (unsigned j = 0, sz = extra_lits.size(); !found && j < sz; ++j) {
                        cube[i] = extra_lits.get(j);
                        if (pt.check_inductive(lemma->level(), cube, uses_level, weakness)) {
                            num_failures = 0;
                            dirty = true;
                            found = true;
                            processed.push_back(extra_lits.get(j));
                            for (i = 0; i < cube.size() &&
                                     processed.contains(cube.get(i)); ++i);
                        }
                    }
                }
                if (!found) {
                    cube[i] = lit;
                    processed.push_back(lit);
                    ++num_failures;
                    ++m_st.num_failures;
                    ++i;
                }
                std::time_t after_expand_lits = std::time(nullptr);
                // TRACE("spacer.ind_gen", tout<<"\t\tfinished expand lit in:"<<after_expand_lits - after_check_ind <<"\n";);
            }else{
                cube[i] = lit;
                processed.push_back(lit);
                ++num_failures;
                ++m_st.num_failures;
                ++i;
            }
         }
    }

    if (dirty) { //temporary disable dirty check to dump all lemmas
    // if(true){
        TRACE("spacer.ind_gen",
               tout << "Generalized from:\n" << mk_and(lemma->get_cube())
               << "\ninto\n" << mk_and(cube) << "\n";);
        lemma->update_cube(lemma->get_pob(), cube);
        SASSERT(uses_level >= lemma->level());
        lemma->set_level(uses_level);
        //lemma->get_expr() is scary. Comment out the following line for now
        //TRACE("spacer.ind_gen", tout<<"SUCCESS. new lemma:"<<lemma->get_expr()->get_id()<<"\n";);
    }
}

void lemma_bool_inductive_generalizer::collect_statistics(statistics &st) const
{
    st.update("time.spacer.solve.reach.gen.bool_ind", m_st.watch.get_seconds());
    st.update("bool inductive gen", m_st.count);
    st.update("bool inductive gen failures", m_st.num_failures);
}

// ------------------------
// h_inductive_generalizer
/// Inductive generalization by dropping and expanding literals with some heuristics
void h_inductive_generalizer::operator()(lemma_ref &lemma) {
  if (lemma->get_cube().empty())
    return;
  TRACE("spacer.h_ind_gen", tout << "LEMMA:\n"
                                 << mk_and(lemma->get_cube()) << "\n";);
  // STRACE("spacer.ind_gen", tout<<"POB:\n"<<lemma->get_pob()<<"\n";);

  // STRACE("spacer.ind_gen", tout<<"USE LIT
  // EXPANSION?\n"<<m_use_expansion<<"\n";);
  m_st.count++;
  TRACE("spacer.h_ind_gen", tout << "m_st.count:" << m_st.count << "\n";);
  STRACE("spacer.h_ind_gen",
         tout << "1st_seen_can_drop:" << m_1st_seen_can_drop << ", "
              << "1st_seen_cannot_drop:" << m_1st_seen_cannot_drop << ", "
              << "ratio:"
              << m_1st_seen_can_drop /
                     (m_1st_seen_cannot_drop + m_1st_seen_can_drop)
              << "\n";);
  scoped_watch _w_(m_st.watch);

  unsigned uses_level;
  pred_transformer &pt = lemma->get_pob()->pt();
  ast_manager &m = pt.get_ast_manager();

  expr_ref_vector cube(m);
  cube.append(lemma->get_cube());

  bool dirty = false;
  expr_ref true_expr(m.mk_true(), m);
  ptr_vector<expr> processed;
  expr_ref_vector extra_lits(m);

  unsigned weakness = lemma->weakness();

  unsigned i = 0, num_failures = 0;

  while (i < cube.size() &&
         (!m_failure_limit || num_failures < m_failure_limit)) {
    expr_ref lit(m);
    lit = cube.get(i);
    increase_lit_count(lit);

    if (should_try_drop(lit)) {
      cube[i] = true_expr;
      if (cube.size() > 1 &&
          pt.check_inductive(lemma->level(), cube, uses_level, weakness)) {
        num_failures = 0;
        dirty = true;
        for (i = 0; i < cube.size() && processed.contains(cube.get(i)); ++i)
          ;
        // drop successful. check and increase m_1st_seen_can_drop
        if (m_lit2count[lit].first == 1) {
          m_1st_seen_can_drop++;
        }
        // increase the success counter
        m_lit2count[lit].second++;
      } else {
        // drop unsuccessful. check and increase m_1st_seen_cannot_drop
        if (m_lit2count[lit].first == 1) {
          m_1st_seen_cannot_drop++;
        }
        cube[i] = lit;
        processed.push_back(lit);
        ++num_failures;
        ++m_st.num_failures;
        ++i;
      }
    } else {
        // skip dropping this literal
        ++i;
        TRACE("spacer.h_ind_gen", tout << lit << ": "
                                       << "Do not try to drop."
                                       << "\n";);
        // should we decrease seen_counter?
        m_lit2count[lit].first --;
    }
  }
  if (dirty) {
    TRACE("spacer.h_ind_gen", tout << "Generalized from:\n"
                                   << mk_and(lemma->get_cube()) << "\ninto\n"
                                   << mk_and(cube) << "\n";);
    lemma->update_cube(lemma->get_pob(), cube);
    SASSERT(uses_level >= lemma->level());
    lemma->set_level(uses_level);
  }
  dump_lit_count();
}
void h_inductive_generalizer::collect_statistics(statistics &st) const {
  st.update("time.spacer.solve.reach.gen.bool_ind", m_st.watch.get_seconds());
  st.update("bool inductive gen", m_st.count);
  st.update("bool inductive gen failures", m_st.num_failures);
}

bool h_inductive_generalizer::should_try_drop(expr_ref &lit) {
  switch (m_heu_index) {
  case 1: {
    return (m_st.count < m_threshold || m_lit2count[lit].first > 1);
  } break;
  case 2:
    /*keep the ratio of 1st seen lits that cannot be drop, and make a guess
     * based on that*/
    { // count number of newly seen lits for threshold
      if (m_1st_seen_cannot_drop + m_1st_seen_can_drop < m_threshold ||
          m_lit2count[lit].first > 1) {
        return true;
      }

      // have seen enough data and is a new lit.
      // calculate ratio so far, and flip a coin
      float ratio_so_far = (m_1st_seen_cannot_drop - 1) /
                           (m_1st_seen_cannot_drop + m_1st_seen_can_drop - 2);

      float flipped_value = float(m_random()) / float(m_random.max_value());

      STRACE("spacer.h_ind_gen",
             tout << "ratio_so_far:" << ratio_so_far
                  << ". Flipped value:" << flipped_value << "should_try_drop:"
                  << bool(flipped_value < ratio_so_far) << "\n";);
      return flipped_value < ratio_so_far;
    }
    break;
  case 3: {
    /*
      if not a new lit, use the success rate of dropping the lit so far
      if a new lit, use 2nd heuristic.
     */

    float seen = m_lit2count[lit].first;
    float success = m_lit2count[lit].second;
    float ratio = success / seen;

    //not enough data. Try to drop.
    if (m_1st_seen_cannot_drop + m_1st_seen_can_drop < m_threshold){
        return true;
    }
    //is a new lit. use 2nd heuristic
    if(seen == 1){
      float ratio_so_far = (m_1st_seen_cannot_drop - 1) /
                           (m_1st_seen_cannot_drop + m_1st_seen_can_drop - 2);

      float flipped_value = float(m_random()) / float(m_random.max_value());

      STRACE("spacer.h_ind_gen",
             tout << "ratio_so_far:" << ratio_so_far
                  << ". Flipped value:" << flipped_value << "should_try_drop:"
                  << bool(flipped_value < ratio_so_far) << "\n";);
      return flipped_value < ratio_so_far;
    }else{
      // not a new lit
      // not successful in the past. Dont try to drop.
      if (ratio < SUCCESS_THRES) {
        STRACE("spacer.h_ind_gen",
               tout << "success ratio:" << ratio
                    << ". SUCCESS_THRES:" << SUCCESS_THRES << "should_try_drop:"
                    << bool(ratio < SUCCESS_THRES) << "\n";);
        return false;
      }else{
          // the lit is not a new lit. Dropping it in the past was successful
          return true;
      }
    }

    //this line should never be reached;
    SASSERT(false);
    return true;
  } break;
  case 4:
      {
    /*
      only use the success rate of dropping the lit so far
     */
    float seen = m_lit2count[lit].first;
    float success = m_lit2count[lit].second;
    float ratio = success / seen;

    //not enough data. Try to drop.
    if (m_1st_seen_cannot_drop + m_1st_seen_can_drop < m_threshold){
        return true;
    }
    // note that newly seen lit will always has the ratio of 0.-> Always got skip
    if (ratio < SUCCESS_THRES) {
      STRACE("spacer.h_ind_gen", tout << "success ratio:" << ratio
                                      << ". SUCCESS_THRES:" << SUCCESS_THRES
                                      << "should_try_drop:"
                                      << bool(ratio < SUCCESS_THRES) << "\n";);
      return false;
    }
  } break;
  case 5: {
    /*
      same as heu 3, but stochastic
     */

    float seen = m_lit2count[lit].first;
    float success = m_lit2count[lit].second;
    float ratio = success / seen;

    //not enough data. Try to drop.
    if (m_1st_seen_cannot_drop + m_1st_seen_can_drop < m_threshold){
        return true;
    }
    //is a new lit. use 2nd heuristic
    const float flipped_value = float(m_random()) / float(m_random.max_value());
    if (seen == 1) {
      float ratio_so_far = (m_1st_seen_cannot_drop - 1) /
                           (m_1st_seen_cannot_drop + m_1st_seen_can_drop - 2);

      return flipped_value < ratio_so_far;
    } else {
      return flipped_value < ratio;
    }

    //this line should never be reached;
    SASSERT(false);
    return true;
  } break;
  case 6: {
    /*
      same as heu 4, but stochastic
     */
    float seen = m_lit2count[lit].first;
    float success = m_lit2count[lit].second;
    float ratio = success / seen;

    // not enough data. Try to drop.
    if (m_1st_seen_cannot_drop + m_1st_seen_can_drop < m_threshold) {
      return true;
    }
    // note that newly seen lit will always has the ratio of 0.-> Always got
    // skip
    const float flipped_value = float(m_random()) / float(m_random.max_value());
    return flipped_value < ratio;
  } break;
  }
  // default value
  return true;
}
void h_inductive_generalizer::increase_lit_count(expr_ref &lit) {
  if (m_lit2count.contains(lit)) {
    STRACE("spacer.h_ind_gen", tout << "LIT:" << lit << " exists."
                                    << "\n";);
    m_lit2count[lit].first++;
  } else {
    STRACE("spacer.h_ind_gen", tout << "LIT:" << lit
                                    << " doesnt exist. Adding to lit2time"
                                    << "\n";);
    m_lit2count.insert(lit, std::pair<unsigned, unsigned> (1, 0));
    m_lits.push_back(lit);
  }
}
void h_inductive_generalizer::dump_lit_count() {
    for (obj_map<expr, std::pair<unsigned, unsigned>>::iterator it = m_lit2count.begin(); it != m_lit2count.end();
       it++) {
        float seen = it->m_value.first;
        float success = it->m_value.second;
        float ratio = success/seen;
        STRACE("spacer.h_ind_gen", tout << mk_pp(it->m_key, m)
               << ": seen: " << seen <<", "
               <<"drop successfully: "<<success <<", "
               <<"success ratio:"<< ratio << "\n";);
  }
}
void unsat_core_generalizer::operator()(lemma_ref &lemma) {
  m_st.count++;
  scoped_watch _w_(m_st.watch);
  ast_manager &m = lemma->get_ast_manager();

  pred_transformer &pt = lemma->get_pob()->pt();

  unsigned old_sz = lemma->get_cube().size();
  unsigned old_level = lemma->level();
  (void)old_level;

  unsigned uses_level;
  expr_ref_vector core(m);
  VERIFY(pt.is_invariant(lemma->level(), lemma.get(), uses_level, &core));

  CTRACE("spacer", old_sz > core.size(),
         tout << "unsat core reduced lemma from: " << old_sz << " to "
              << core.size() << "\n";);
  CTRACE("spacer", old_level < uses_level,
         tout << "unsat core moved lemma up from: " << old_level << " to "
              << uses_level << "\n";);
  if (old_sz > core.size()) {
    lemma->update_cube(lemma->get_pob(), core);
    lemma->set_level(uses_level);
  }
}

void unsat_core_generalizer::collect_statistics(statistics &st) const
{
    st.update("time.spacer.solve.reach.gen.unsat_core", m_st.watch.get_seconds());
    st.update("gen.unsat_core.cnt", m_st.count);
    st.update("gen.unsat_core.fail", m_st.num_failures);
}

namespace {
class collect_array_proc {
    array_util m_au;
    func_decl_set &m_symbs;
    sort *m_sort;
public:
    collect_array_proc(ast_manager &m, func_decl_set& s) :
        m_au(m), m_symbs(s), m_sort(nullptr) {}

    void operator()(app* a)
    {
        if (a->get_family_id() == null_family_id && m_au.is_array(a)) {
            if (m_sort && m_sort != get_sort(a)) { return; }
            if (!m_sort) { m_sort = get_sort(a); }
            m_symbs.insert(a->get_decl());
        }
    }
    void operator()(var*) {}
    void operator()(quantifier*) {}
};
}

bool lemma_array_eq_generalizer::is_array_eq (ast_manager &m, expr* e) {

    expr *e1 = nullptr, *e2 = nullptr;
    if (m.is_eq(e, e1, e2) && is_app(e1) && is_app(e2)) {
        app *a1 = to_app(e1);
        app *a2 = to_app(e2);
        array_util au(m);
        if (a1->get_family_id() == null_family_id &&
            a2->get_family_id() == null_family_id &&
            au.is_array(a1) && au.is_array(a2))
            return true;
    }
    return false;
}

void lemma_array_eq_generalizer::operator() (lemma_ref &lemma)
{

    ast_manager &m = lemma->get_ast_manager();


    expr_ref_vector core(m);
    expr_ref v(m);
    func_decl_set symb;
    collect_array_proc cap(m, symb);


    // -- find array constants
    core.append (lemma->get_cube());
    v = mk_and(core);
    for_each_expr(cap, v);

    CTRACE("core_array_eq", symb.size() > 1 && symb.size() <= 8,
          tout << "found " << symb.size() << " array variables in: \n"
          << v << "\n";);

    // too few constants or too many constants
    if (symb.size() <= 1 || symb.size() > 8) { return; }


    // -- for every pair of constants (A, B), check whether the
    // -- equality (A=B) generalizes a literal in the lemma

    ptr_vector<func_decl> vsymbs;
    for (auto * fdecl : symb) {vsymbs.push_back(fdecl);}

    // create all equalities
    expr_ref_vector eqs(m);
    for (unsigned i = 0, sz = vsymbs.size(); i < sz; ++i) {
        for (unsigned j = i + 1; j < sz; ++j) {
            eqs.push_back(m.mk_eq(m.mk_const(vsymbs.get(i)),
                                  m.mk_const(vsymbs.get(j))));
        }
    }

    // smt-solver to check whether a literal is generalized.  using
    // default params. There has to be a simpler way to approximate
    // this check
    ref<solver> sol = mk_smt_solver(m, params_ref::get_empty(), symbol::null);
    // literals of the new lemma
    expr_ref_vector lits(m);
    lits.append(core);
    expr *t = nullptr;
    bool dirty = false;
    for (unsigned i = 0, sz = core.size(); i < sz; ++i) {
        // skip a literal is it is already an array equality
        if (m.is_not(lits.get(i), t) && is_array_eq(m, t)) continue;
        solver::scoped_push _pp_(*sol);
        sol->assert_expr(lits.get(i));
        for (auto *e : eqs) {
            solver::scoped_push _p_(*sol);
            sol->assert_expr(e);
            lbool res = sol->check_sat(0, nullptr);

            if (res == l_false) {
                TRACE("core_array_eq",
                      tout << "strengthened " << mk_pp(lits.get(i), m)
                      << " with " << mk_pp(mk_not(m, e), m) << "\n";);
                lits[i] = mk_not(m, e);
                dirty = true;
                break;
            }
        }
    }

    // nothing changed
    if (!dirty) return;

    TRACE("core_array_eq",
           tout << "new possible core " << mk_and(lits) << "\n";);


    pred_transformer &pt = lemma->get_pob()->pt();
    // -- check if the generalized result is consistent with trans
    unsigned uses_level1;
    if (pt.check_inductive(lemma->level(), lits, uses_level1, lemma->weakness())) {
        TRACE("core_array_eq", tout << "Inductive!\n";);
        lemma->update_cube(lemma->get_pob(), lits);
        lemma->set_level(uses_level1);
    }
    else
    {TRACE("core_array_eq", tout << "Not-Inductive!\n";);}
}

void lemma_eq_generalizer::operator() (lemma_ref &lemma)
{
    TRACE("core_eq", tout << "Transforming equivalence classes\n";);

    if (lemma->get_cube().empty()) return;

    ast_manager &m = m_ctx.get_ast_manager();
    qe::term_graph egraph(m);
    egraph.add_lits(lemma->get_cube());

    // -- expand the cube with all derived equalities
    expr_ref_vector core(m);
    egraph.to_lits(core, true);

    // -- if the core looks different from the original cube
    if (core.size() != lemma->get_cube().size() ||
        core.get(0) != lemma->get_cube().get(0)) {
        // -- update the lemma
        lemma->update_cube(lemma->get_pob(), core);
    }
}


};
