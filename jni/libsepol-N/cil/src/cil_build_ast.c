/*
 * Copyright 2011 Tresys Technology, LLC. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 * 
 *    2. Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY TRESYS TECHNOLOGY, LLC ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL TRESYS TECHNOLOGY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of Tresys Technology, LLC.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sepol/policydb/conditional.h>

#include "cil_internal.h"
#include "cil_flavor.h"
#include "cil_log.h"
#include "cil_mem.h"
#include "cil_tree.h"
#include "cil_list.h"
#include "cil_parser.h"
#include "cil_build_ast.h"
#include "cil_copy_ast.h"
#include "cil_verify.h"
#include "cil_strpool.h"

struct cil_args_build {
	struct cil_tree_node *ast;
	struct cil_db *db;
	struct cil_tree_node *macro;
	struct cil_tree_node *boolif;
	struct cil_tree_node *tunif;
	struct cil_tree_node *in;
};

int cil_fill_list(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list **list)
{
	int rc = SEPOL_ERR;
	struct cil_tree_node *curr;
	enum cil_syntax syntax[] = {
		CIL_SYN_N_STRINGS,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
 
	rc = __cil_verify_syntax(current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
 	}

	cil_list_init(list, flavor);

	for (curr = current; curr != NULL; curr = curr->next) {
		cil_list_append(*list, CIL_STRING, curr->data);
	}

	return SEPOL_OK;

exit:
	return rc;
}

int cil_gen_node(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *ast_node, struct cil_symtab_datum *datum, hashtab_key_t key, enum cil_sym_index sflavor, enum cil_flavor nflavor)
{
	int rc = SEPOL_ERR;
	symtab_t *symtab = NULL;

	rc = __cil_verify_name((const char*)key);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_get_symtab(ast_node->parent, &symtab, sflavor);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = datum;
	ast_node->flavor = nflavor;

	if (symtab != NULL) {
		rc = cil_symtab_insert(symtab, (hashtab_key_t)key, datum, ast_node);
		if (rc == SEPOL_EEXIST) {
			cil_log(CIL_ERR, "Re-declaration of %s %s\n", 
				cil_node_to_string(ast_node), key);
			if (cil_symtab_get_datum(symtab, key, &datum) == SEPOL_OK) {
				if (sflavor == CIL_SYM_BLOCKS) {
					struct cil_tree_node *node = datum->nodes->head->data;
					cil_log(CIL_ERR, "Previous declaration at line %d of %s\n",
						node->line, node->path);
				}
			}
			goto exit;
		}
	}

	if (ast_node->flavor >= CIL_MIN_DECLARATIVE && ast_node->parent->flavor == CIL_MACRO) {
		struct cil_list_item *item;
		struct cil_list *param_list = ((struct cil_macro*)ast_node->parent->data)->params;
		if (param_list != NULL) {
			cil_list_for_each(item, param_list) {
				struct cil_param *param = item->data;
				if (param->flavor == ast_node->flavor) {
					if (param->str == key) {
						cil_log(CIL_ERR, "%s %s shadows a macro parameter in macro declaration\n", cil_node_to_string(ast_node), key);
						rc = SEPOL_ERR;
						goto exit;
					}
				}
			}
		}
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Failed to create node\n");
	return rc;
}

void cil_clear_node(struct cil_tree_node *ast_node)
{
	if (ast_node == NULL) {
		return;
	}

	ast_node->data = NULL;
	ast_node->flavor = CIL_NONE;
}

int cil_gen_block(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, uint16_t is_abstract)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_N_LISTS | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_block *block = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_block_init(&block);

	block->is_abstract = is_abstract;

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)block, (hashtab_key_t)key, CIL_SYM_BLOCKS, CIL_BLOCK);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad block declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_block(block);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_block(struct cil_block *block)
{
	if (block == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&block->datum);
	cil_symtab_array_destroy(block->symtab);
	cil_list_destroy(&block->bi_nodes, CIL_FALSE);

	free(block);
}

int cil_gen_blockinherit(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_blockinherit *inherit = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_blockinherit_init(&inherit);

	inherit->block_str = parse_current->next->data;

	ast_node->data = inherit;
	ast_node->flavor = CIL_BLOCKINHERIT;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad blockinherit declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_blockinherit(inherit);
	return rc;
}

void cil_destroy_blockinherit(struct cil_blockinherit *inherit)
{
	if (inherit == NULL) {
		return;
	}

	free(inherit);
}

int cil_gen_blockabstract(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_blockabstract *abstract = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_blockabstract_init(&abstract);

	abstract->block_str = parse_current->next->data;

	ast_node->data = abstract;
	ast_node->flavor = CIL_BLOCKABSTRACT;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad blockabstract declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_blockabstract(abstract);
	return rc;
}

void cil_destroy_blockabstract(struct cil_blockabstract *abstract)
{
	if (abstract == NULL) {
		return;
	}

	free(abstract);
}

int cil_gen_in(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_N_LISTS,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_in *in = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_in_init(&in);

	in->block_str = parse_current->next->data;

	ast_node->data = in;
	ast_node->flavor = CIL_IN;

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad in statement at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_in(in);
	return rc;
}

void cil_destroy_in(struct cil_in *in)
{
	if (in == NULL) {
		return;
	}

	cil_symtab_array_destroy(in->symtab);

	free(in);
}

int cil_gen_class(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST | CIL_SYN_EMPTY_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_class *class = NULL;
	struct cil_tree_node *perms = NULL;
	int rc = SEPOL_ERR;

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_class_init(&class);

	key = parse_current->next->data;
	if (key == CIL_KEY_UNORDERED) {
		cil_log(CIL_ERR, "'unordered' keyword is reserved and not a valid class name.\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)class, (hashtab_key_t)key, CIL_SYM_CLASSES, CIL_CLASS);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->next != NULL) {
		perms = parse_current->next->next->cl_head;
		rc = cil_gen_perm_nodes(db, perms, ast_node, CIL_PERM, &class->num_perms);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad class declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_class(class);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_class(struct cil_class *class)
{
	if (class == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&class->datum);
	cil_symtab_destroy(&class->perms);

	free(class);
}

int cil_gen_classorder(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_classorder *classorder = NULL;
	struct cil_list_item *curr = NULL;
	struct cil_list_item *head = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc !=  SEPOL_OK) {
		goto exit;
	}

	cil_classorder_init(&classorder);

	rc = cil_fill_list(parse_current->next->cl_head, CIL_CLASSORDER, &classorder->class_list_str);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	head = classorder->class_list_str->head;
	cil_list_for_each(curr, classorder->class_list_str) {
		if (curr->data == CIL_KEY_UNORDERED) {
			if (curr == head && curr->next == NULL) {
				cil_log(CIL_ERR, "Classorder 'unordered' keyword must be followed by one or more class.\n");
				rc = SEPOL_ERR;
				goto exit;
			} else if (curr != head) {
				cil_log(CIL_ERR, "Classorder can only use 'unordered' keyword as the first item in the list.\n");
				rc = SEPOL_ERR;
				goto exit;
			}
		}
	}

	ast_node->data = classorder;
	ast_node->flavor = CIL_CLASSORDER;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad classorder declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_classorder(classorder);
	return rc;
}

void cil_destroy_classorder(struct cil_classorder *classorder)
{
	if (classorder == NULL) {
		return;
	}

	if (classorder->class_list_str != NULL) {
		cil_list_destroy(&classorder->class_list_str, 1);
	}

	free(classorder);
}

int cil_gen_perm(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor, unsigned int *num_perms)
{
	char *key = NULL;
	struct cil_perm *perm = NULL;
	int rc = SEPOL_ERR;

	cil_perm_init(&perm);

	key = parse_current->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)perm, (hashtab_key_t)key, CIL_SYM_PERMS, flavor);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	perm->value = *num_perms;
	(*num_perms)++;

	return SEPOL_OK;

exit:
	cil_destroy_perm(perm);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_perm(struct cil_perm *perm)
{
	if (perm == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&perm->datum);
	cil_list_destroy(&perm->classperms, CIL_FALSE);

	free(perm);
}

int cil_gen_perm_nodes(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *current_perm, struct cil_tree_node *ast_node, enum cil_flavor flavor, unsigned int *num_perms)
{
	int rc = SEPOL_ERR;
	struct cil_tree_node *new_ast = NULL;

	while(current_perm != NULL) {
		if (current_perm->cl_head != NULL) {
		
			rc = SEPOL_ERR;
			goto exit;
		}
		cil_tree_node_init(&new_ast);
		new_ast->parent = ast_node;
		new_ast->line = current_perm->line;
		new_ast->path = current_perm->path;

		rc = cil_gen_perm(db, current_perm, new_ast, flavor, num_perms);
		if (rc != SEPOL_OK) {
			goto exit;
		}

		if (ast_node->cl_head == NULL) {
			ast_node->cl_head = new_ast;
		} else {
			ast_node->cl_tail->next = new_ast;
		}
		ast_node->cl_tail = new_ast;

		current_perm = current_perm->next;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad permissions\n");
	return rc;
}

int cil_fill_perms(struct cil_tree_node *start_perm, struct cil_list **perms)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_N_STRINGS | CIL_SYN_N_LISTS,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	rc = __cil_verify_syntax(start_perm->cl_head, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_expr(start_perm, CIL_PERM, perms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad permission list or expression\n");
	return rc;
}

int cil_fill_classperms(struct cil_tree_node *parse_current, struct cil_classperms **cp)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_classperms_init(cp);

	(*cp)->class_str = parse_current->data;

	rc = cil_fill_perms(parse_current->next, &(*cp)->perm_strs);
	if (rc != SEPOL_OK) {
		cil_destroy_classperms(*cp);
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad class-permissions\n");
	*cp = NULL;
	return rc;
}

void cil_destroy_classperms(struct cil_classperms *cp)
{
	if (cp == NULL) {
		return;
	}

	cil_list_destroy(&cp->perm_strs, CIL_TRUE);
	cil_list_destroy(&cp->perms, CIL_FALSE);

	free(cp);
}

void cil_fill_classperms_set(struct cil_tree_node *parse_current, struct cil_classperms_set **cp_set)
{
	cil_classperms_set_init(cp_set);
	(*cp_set)->set_str = parse_current->data;
}

void cil_destroy_classperms_set(struct cil_classperms_set *cp_set)
{
	if (cp_set == NULL) {
		return;
	}

	free(cp_set);
}

int cil_fill_classperms_list(struct cil_tree_node *parse_current, struct cil_list **cp_list)
{
	int rc = SEPOL_ERR;
	struct cil_tree_node *curr;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING | CIL_SYN_LIST,
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	if (parse_current == NULL || cp_list == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_list_init(cp_list, CIL_CLASSPERMS);

	curr = parse_current->cl_head;

	if (curr == NULL) {
		/* Class-perms form: SET1 */
		struct cil_classperms_set *new_cp_set;
		cil_fill_classperms_set(parse_current, &new_cp_set);
		cil_list_append(*cp_list, CIL_CLASSPERMS_SET, new_cp_set);
	} else if (curr->cl_head == NULL) {
		/* Class-perms form: (CLASS1 (PERM1 ...)) */
		struct cil_classperms *new_cp;
		rc = cil_fill_classperms(curr, &new_cp);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_append(*cp_list, CIL_CLASSPERMS, new_cp);
	} else {
		cil_log(CIL_ERR, "Bad class-permissions list syntax\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Problem filling class-permissions list\n");
	cil_list_destroy(cp_list, CIL_TRUE);
	return rc;
}

void cil_destroy_classperms_list(struct cil_list **cp_list)
{
	struct cil_list_item *curr;

	if (cp_list == NULL || *cp_list == NULL) {
		return;
	}

	cil_list_for_each(curr, *cp_list) {
		if (curr->flavor == CIL_CLASSPERMS) {
			cil_destroy_classperms(curr->data);
		} else {
			cil_destroy_classperms_set(curr->data);
		}
	}

	cil_list_destroy(cp_list, CIL_FALSE);
}

int cil_gen_classpermission(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	char *key = NULL;
	struct cil_classpermission *cp = NULL;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_classpermission_init(&cp);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)cp, (hashtab_key_t)key, CIL_SYM_CLASSPERMSETS, CIL_CLASSPERMISSION);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad classpermission declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_classpermission(cp);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_classpermission(struct cil_classpermission *cp)
{
	if (cp == NULL) {
		return;
	}

	if (cp->datum.name != NULL) {
		cil_list_destroy(&cp->classperms, CIL_FALSE);
	} else {
		/* anonymous classpermission from call */
		cil_destroy_classperms_list(&cp->classperms);
	}

	cil_symtab_datum_destroy(&cp->datum);


	free(cp);
}

int cil_gen_classpermissionset(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	struct cil_classpermissionset *cps = NULL;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_classpermissionset_init(&cps);

	cps->set_str = parse_current->next->data;

	rc = cil_fill_classperms_list(parse_current->next->next, &cps->classperms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = cps;
	ast_node->flavor = CIL_CLASSPERMISSIONSET;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad classpermissionset at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_classpermissionset(cps);
	return rc;
}

void cil_destroy_classpermissionset(struct cil_classpermissionset *cps)
{
	if (cps == NULL) {
		return;
	}

	cil_destroy_classperms_list(&cps->classperms);

	free(cps);
}

int cil_gen_map_class(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_class *map = NULL;
	int rc = SEPOL_ERR;

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_class_init(&map);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)map, (hashtab_key_t)key, CIL_SYM_CLASSES, CIL_MAP_CLASS);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_perm_nodes(db, parse_current->next->next->cl_head, ast_node, CIL_MAP_PERM, &map->num_perms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad map class declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_class(map);
	cil_clear_node(ast_node);
	return rc;
}

int cil_gen_classmapping(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	struct cil_classmapping *mapping = NULL;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_classmapping_init(&mapping);

	mapping->map_class_str = parse_current->next->data;
	mapping->map_perm_str = parse_current->next->next->data;

	rc = cil_fill_classperms_list(parse_current->next->next->next, &mapping->classperms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = mapping;
	ast_node->flavor = CIL_CLASSMAPPING;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad classmapping declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_classmapping(mapping);
	return rc;
}

void cil_destroy_classmapping(struct cil_classmapping *mapping)
{
	if (mapping == NULL) {
		return;
	}

	cil_destroy_classperms_list(&mapping->classperms);

	free(mapping);
}

// TODO try to merge some of this with cil_gen_class (helper function for both)
int cil_gen_common(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_class *common = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_class_init(&common);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)common, (hashtab_key_t)key, CIL_SYM_COMMONS, CIL_COMMON);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_perm_nodes(db, parse_current->next->next->cl_head, ast_node, CIL_PERM, &common->num_perms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad common declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_class(common);
	cil_clear_node(ast_node);
	return rc;

}

int cil_gen_classcommon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_classcommon *clscom = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_classcommon_init(&clscom);

	clscom->class_str = parse_current->next->data;
	clscom->common_str = parse_current->next->next->data;

	ast_node->data = clscom;
	ast_node->flavor = CIL_CLASSCOMMON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad classcommon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_classcommon(clscom);
	return rc;

}

void cil_destroy_classcommon(struct cil_classcommon *clscom)
{
	if (clscom == NULL) {
		return;
	}

	free(clscom);
}

int cil_gen_sid(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_sid *sid = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_sid_init(&sid);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)sid, (hashtab_key_t)key, CIL_SYM_SIDS, CIL_SID);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sid declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_sid(sid);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_sid(struct cil_sid *sid)
{
	if (sid == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&sid->datum);
	free(sid);
}

int cil_gen_sidcontext(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_sidcontext *sidcon = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_sidcontext_init(&sidcon);

	sidcon->sid_str = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		sidcon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&sidcon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, sidcon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = sidcon;
	ast_node->flavor = CIL_SIDCONTEXT;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sidcontext declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_sidcontext(sidcon);
	return rc;
}

void cil_destroy_sidcontext(struct cil_sidcontext *sidcon)
{
	if (sidcon == NULL) {
		return;
	}

	if (sidcon->context_str == NULL && sidcon->context != NULL) {
		cil_destroy_context(sidcon->context);
	}

	free(sidcon);
}

int cil_gen_sidorder(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_sidorder *sidorder = NULL;
	struct cil_list_item *curr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc !=  SEPOL_OK) {
		goto exit;
	}

	cil_sidorder_init(&sidorder);

	rc = cil_fill_list(parse_current->next->cl_head, CIL_SIDORDER, &sidorder->sid_list_str);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_list_for_each(curr, sidorder->sid_list_str) {
		if (curr->data == CIL_KEY_UNORDERED) {
			cil_log(CIL_ERR, "Sidorder cannot be unordered.\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	ast_node->data = sidorder;
	ast_node->flavor = CIL_SIDORDER;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sidorder declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_sidorder(sidorder);
	return rc;
}

void cil_destroy_sidorder(struct cil_sidorder *sidorder)
{
	if (sidorder == NULL) {
		return;
	}

	if (sidorder->sid_list_str != NULL) {
		cil_list_destroy(&sidorder->sid_list_str, 1);
	}

	free(sidorder);
}

int cil_gen_user(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_user *user = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_user_init(&user);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)user, (hashtab_key_t)key, CIL_SYM_USERS, CIL_USER);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad user declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_user(user);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_user(struct cil_user *user)
{
	if (user == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&user->datum);
	ebitmap_destroy(user->roles);
	free(user->roles);
	free(user);
}

int cil_gen_userattribute(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_userattribute *attr = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userattribute_init(&attr);

	key = parse_current->next->data;
	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)attr, (hashtab_key_t)key, CIL_SYM_USERS, CIL_USERATTRIBUTE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad userattribute declaration at line %d of %s\n",
		parse_current->line, parse_current->path);
	cil_destroy_userattribute(attr);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_userattribute(struct cil_userattribute *attr)
{
	struct cil_list_item *expr = NULL;
	struct cil_list_item *next = NULL;

	if (attr == NULL) {
		return;
	}

	if (attr->expr_list != NULL) {
		/* we don't want to destroy the expression stacks (cil_list) inside
		 * this list cil_list_destroy destroys sublists, so we need to do it
		 * manually */
		expr = attr->expr_list->head;
		while (expr != NULL) {
			next = expr->next;
			cil_list_item_destroy(&expr, CIL_FALSE);
			expr = next;
		}
		free(attr->expr_list);
		attr->expr_list = NULL;
	}

	cil_symtab_datum_destroy(&attr->datum);
	ebitmap_destroy(attr->users);
	free(attr->users);
	free(attr);
}

int cil_gen_userattributeset(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_userattributeset *attrset = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userattributeset_init(&attrset);

	attrset->attr_str = parse_current->next->data;

	rc = cil_gen_expr(parse_current->next->next, CIL_USER, &attrset->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	ast_node->data = attrset;
	ast_node->flavor = CIL_USERATTRIBUTESET;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad userattributeset declaration at line %d of %s\n",
		parse_current->line, parse_current->path);
	cil_destroy_userattributeset(attrset);

	return rc;
}

void cil_destroy_userattributeset(struct cil_userattributeset *attrset)
{
	if (attrset == NULL) {
		return;
	}

	cil_list_destroy(&attrset->str_expr, CIL_TRUE);
	cil_list_destroy(&attrset->datum_expr, CIL_FALSE);

	free(attrset);
}

int cil_gen_userlevel(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_userlevel *usrlvl = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userlevel_init(&usrlvl);

	usrlvl->user_str = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		usrlvl->level_str = parse_current->next->next->data;
	} else {
		cil_level_init(&usrlvl->level);

		rc = cil_fill_level(parse_current->next->next->cl_head, usrlvl->level);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = usrlvl;
	ast_node->flavor = CIL_USERLEVEL;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad userlevel declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_userlevel(usrlvl);
	return rc;
}

void cil_destroy_userlevel(struct cil_userlevel *usrlvl)
{
	if (usrlvl == NULL) {
		return;
	}

	if (usrlvl->level_str == NULL && usrlvl->level != NULL) {
		cil_destroy_level(usrlvl->level);
	}

	free(usrlvl);
}

int cil_gen_userrange(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_userrange *userrange = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userrange_init(&userrange);

	userrange->user_str = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		userrange->range_str = parse_current->next->next->data;
	} else {
		cil_levelrange_init(&userrange->range);

		rc = cil_fill_levelrange(parse_current->next->next->cl_head, userrange->range);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = userrange;
	ast_node->flavor = CIL_USERRANGE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad userrange declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_userrange(userrange);
	return rc;
}

void cil_destroy_userrange(struct cil_userrange *userrange)
{
	if (userrange == NULL) {
		return;
	}

	if (userrange->range_str == NULL && userrange->range != NULL) {
		cil_destroy_levelrange(userrange->range);
	}

	free(userrange);
}

int cil_gen_userprefix(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_userprefix *userprefix = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userprefix_init(&userprefix);

	userprefix->user_str = parse_current->next->data;
	userprefix->prefix_str = parse_current->next->next->data;

	ast_node->data = userprefix;
	ast_node->flavor = CIL_USERPREFIX;

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad userprefix declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_userprefix(userprefix);
	return rc;
}

void cil_destroy_userprefix(struct cil_userprefix *userprefix)
{
	if (userprefix == NULL) {
		return;
	}

	free(userprefix);
}

int cil_gen_selinuxuser(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_selinuxuser *selinuxuser = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_selinuxuser_init(&selinuxuser);

	selinuxuser->name_str = parse_current->next->data;
	selinuxuser->user_str = parse_current->next->next->data;

	if (parse_current->next->next->next->cl_head == NULL) {
		selinuxuser->range_str = parse_current->next->next->next->data;
	} else {
		cil_levelrange_init(&selinuxuser->range);

		rc = cil_fill_levelrange(parse_current->next->next->next->cl_head, selinuxuser->range);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = selinuxuser;
	ast_node->flavor = CIL_SELINUXUSER;

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad selinuxuser declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_selinuxuser(selinuxuser);
	return rc;
}

int cil_gen_selinuxuserdefault(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_selinuxuser *selinuxuser = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_selinuxuser_init(&selinuxuser);

	selinuxuser->name_str = cil_strpool_add("__default__");
	selinuxuser->user_str = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		selinuxuser->range_str = parse_current->next->next->data;
	} else {
		cil_levelrange_init(&selinuxuser->range);

		rc = cil_fill_levelrange(parse_current->next->next->cl_head, selinuxuser->range);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = selinuxuser;
	ast_node->flavor = CIL_SELINUXUSERDEFAULT;

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad selinuxuserdefault declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_selinuxuser(selinuxuser);
	return rc;
}

void cil_destroy_selinuxuser(struct cil_selinuxuser *selinuxuser)
{
	if (selinuxuser == NULL) {
		return;
	}

	if (selinuxuser->range_str == NULL && selinuxuser->range != NULL) {
		cil_destroy_levelrange(selinuxuser->range);
	}

	free(selinuxuser);
}

int cil_gen_role(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_role *role = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_role_init(&role);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)role, (hashtab_key_t)key, CIL_SYM_ROLES, CIL_ROLE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad role declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_role(role);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_role(struct cil_role *role)
{
	if (role == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&role->datum);
	ebitmap_destroy(role->types);
	free(role->types);
	free(role);
}

int cil_gen_roletype(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_roletype *roletype = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_roletype_init(&roletype);

	roletype->role_str = parse_current->next->data;
	roletype->type_str = parse_current->next->next->data;

	ast_node->data = roletype;
	ast_node->flavor = CIL_ROLETYPE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad roletype declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_roletype(roletype);
	return rc;
}

void cil_destroy_roletype(struct cil_roletype *roletype)
{
	if (roletype == NULL) {
		return;
	}

	free(roletype);
}

int cil_gen_userrole(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_userrole *userrole = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_userrole_init(&userrole);

	userrole->user_str = parse_current->next->data;
	userrole->role_str = parse_current->next->next->data;

	ast_node->data = userrole;
	ast_node->flavor = CIL_USERROLE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad userrole declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_userrole(userrole);
	return rc;
}

void cil_destroy_userrole(struct cil_userrole *userrole)
{
	if (userrole == NULL) {
		return;
	}

	free(userrole);
}

int cil_gen_roletransition(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_roletransition *roletrans = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_roletransition_init(&roletrans);

	roletrans->src_str = parse_current->next->data;
	roletrans->tgt_str = parse_current->next->next->data;
	roletrans->obj_str = parse_current->next->next->next->data;
	roletrans->result_str = parse_current->next->next->next->next->data;

	ast_node->data = roletrans;
	ast_node->flavor = CIL_ROLETRANSITION;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad roletransition rule at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_roletransition(roletrans);
	return rc;
}

void cil_destroy_roletransition(struct cil_roletransition *roletrans)
{
	if (roletrans == NULL) {
		return;
	}

	free(roletrans);
}

int cil_gen_roleallow(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_roleallow *roleallow = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_roleallow_init(&roleallow);

	roleallow->src_str = parse_current->next->data;
	roleallow->tgt_str = parse_current->next->next->data;

	ast_node->data = roleallow;
	ast_node->flavor = CIL_ROLEALLOW;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad roleallow rule at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_roleallow(roleallow);
	return rc;
}

void cil_destroy_roleallow(struct cil_roleallow *roleallow)
{
	if (roleallow == NULL) {
		return;
	}

	free(roleallow);
}

int cil_gen_roleattribute(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_roleattribute *attr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->data == CIL_KEY_SELF) {
		cil_log(CIL_ERR, "The keyword '%s' is reserved\n", CIL_KEY_SELF);
		rc = SEPOL_ERR;
		goto exit;
	}

	cil_roleattribute_init(&attr);

	key = parse_current->next->data;
	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)attr, (hashtab_key_t)key, CIL_SYM_ROLES, CIL_ROLEATTRIBUTE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;
exit:
	cil_log(CIL_ERR, "Bad roleattribute declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_roleattribute(attr);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_roleattribute(struct cil_roleattribute *attr)
{
	if (attr == NULL) {
		return;
	}

	if (attr->expr_list != NULL) {
		/* we don't want to destroy the expression stacks (cil_list) inside
		 * this list cil_list_destroy destroys sublists, so we need to do it
		 * manually */
		struct cil_list_item *expr = attr->expr_list->head;
		while (expr != NULL) {
			struct cil_list_item *next = expr->next;
			cil_list_item_destroy(&expr, CIL_FALSE);
			expr = next;
		}
		free(attr->expr_list);
		attr->expr_list = NULL;
	}

	cil_symtab_datum_destroy(&attr->datum);
	ebitmap_destroy(attr->roles);
	free(attr->roles);
	free(attr);
}

int cil_gen_roleattributeset(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_roleattributeset *attrset = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_roleattributeset_init(&attrset);

	attrset->attr_str = parse_current->next->data;

	rc = cil_gen_expr(parse_current->next->next, CIL_ROLE, &attrset->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	ast_node->data = attrset;
	ast_node->flavor = CIL_ROLEATTRIBUTESET;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad roleattributeset declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_roleattributeset(attrset);

	return rc;
}

void cil_destroy_roleattributeset(struct cil_roleattributeset *attrset)
{
	if (attrset == NULL) {
		return;
	}

	cil_list_destroy(&attrset->str_expr, CIL_TRUE);
	cil_list_destroy(&attrset->datum_expr, CIL_FALSE);

	free(attrset);
}

int cil_gen_avrule(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, uint32_t rule_kind)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_avrule *rule = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_avrule_init(&rule);

	rule->is_extended = 0;
	rule->rule_kind = rule_kind;

	rule->src_str = parse_current->next->data;
	rule->tgt_str = parse_current->next->next->data;

	rc = cil_fill_classperms_list(parse_current->next->next->next, &rule->perms.classperms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = rule;
	ast_node->flavor = CIL_AVRULE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad allow rule at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_avrule(rule);
	return rc;
}

void cil_destroy_avrule(struct cil_avrule *rule)
{
	if (rule == NULL) {
		return;
	}

	if (!rule->is_extended) {
		cil_destroy_classperms_list(&rule->perms.classperms);
	} else {
		if (rule->perms.x.permx_str == NULL && rule->perms.x.permx != NULL) {
			cil_destroy_permissionx(rule->perms.x.permx);
		}
	}

	free(rule);
}

int cil_fill_permissionx(struct cil_tree_node *parse_current, struct cil_permissionx *permx)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->data == CIL_KEY_IOCTL) {
		permx->kind = CIL_PERMX_KIND_IOCTL;
	} else {
		cil_log(CIL_ERR, "Unknown permissionx kind, %s. Must be \"ioctl\"\n", (char *)parse_current->data);
		rc = SEPOL_ERR;
		goto exit;
	}

	permx->obj_str = parse_current->next->data;

	rc = cil_gen_expr(parse_current->next->next, CIL_PERMISSIONX, &permx->expr_str);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad permissionx content at line %d of %s\n",
		parse_current->line, parse_current->path);
	return rc;
}

int cil_gen_permissionx(struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_permissionx *permx = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_permissionx_init(&permx);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)permx, (hashtab_key_t)key, CIL_SYM_PERMX, CIL_PERMISSIONX);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_fill_permissionx(parse_current->next->next->cl_head, permx);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad permissionx statement at line %d of %s\n",
		parse_current->line, parse_current->path);
	cil_destroy_permissionx(permx);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_permissionx(struct cil_permissionx *permx)
{
	if (permx == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&permx->datum);

	cil_list_destroy(&permx->expr_str, CIL_TRUE);
	ebitmap_destroy(permx->perms);
	free(permx->perms);
	free(permx);
}

int cil_gen_avrulex(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, uint32_t rule_kind)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_avrule *rule = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_avrule_init(&rule);

	rule->is_extended = 1;
	rule->rule_kind = rule_kind;
	rule->src_str = parse_current->next->data;
	rule->tgt_str = parse_current->next->next->data;

	if (parse_current->next->next->next->cl_head == NULL) {
		rule->perms.x.permx_str = parse_current->next->next->next->data;
	} else {
		cil_permissionx_init(&rule->perms.x.permx);

		rc = cil_fill_permissionx(parse_current->next->next->next->cl_head, rule->perms.x.permx);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = rule;
	ast_node->flavor = CIL_AVRULEX;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad allowx rule at line %d of %s\n",
		parse_current->line, parse_current->path);
	cil_destroy_avrule(rule);
	return rc;
}

int cil_gen_type_rule(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, uint32_t rule_kind)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_type_rule *rule = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_type_rule_init(&rule);

	rule->rule_kind = rule_kind;
	rule->src_str = parse_current->next->data;
	rule->tgt_str = parse_current->next->next->data;
	rule->obj_str = parse_current->next->next->next->data;
	rule->result_str = parse_current->next->next->next->next->data;

	ast_node->data = rule;
	ast_node->flavor = CIL_TYPE_RULE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad type rule at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_type_rule(rule);
	return rc;
}

void cil_destroy_type_rule(struct cil_type_rule *rule)
{
	if (rule == NULL) {
		return;
	}

	free(rule);
}

int cil_gen_type(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_type *type = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->data == CIL_KEY_SELF) {
		cil_log(CIL_ERR, "The keyword '%s' is reserved\n", CIL_KEY_SELF);
		rc = SEPOL_ERR;
		goto exit;
	}

	cil_type_init(&type);

	key = parse_current->next->data;
	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)type, (hashtab_key_t)key, CIL_SYM_TYPES, CIL_TYPE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad type declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_type(type);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_type(struct cil_type *type)
{
	if (type == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&type->datum);
	free(type);
}

int cil_gen_typeattribute(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_typeattribute *attr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->data == CIL_KEY_SELF) {
		cil_log(CIL_ERR, "The keyword '%s' is reserved\n", CIL_KEY_SELF);
		rc = SEPOL_ERR;
		goto exit;
	}

	cil_typeattribute_init(&attr);

	key = parse_current->next->data;
	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)attr, (hashtab_key_t)key, CIL_SYM_TYPES, CIL_TYPEATTRIBUTE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad typeattribute declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_typeattribute(attr);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_typeattribute(struct cil_typeattribute *attr)
{
	if (attr == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&attr->datum);

	if (attr->expr_list != NULL) {
		/* we don't want to destroy the expression stacks (cil_list) inside
		 * this list cil_list_destroy destroys sublists, so we need to do it
		 * manually */
		struct cil_list_item *expr = attr->expr_list->head;
		while (expr != NULL) {
			struct cil_list_item *next = expr->next;
			cil_list_item_destroy(&expr, CIL_FALSE);
			expr = next;
		}
		free(attr->expr_list);
		attr->expr_list = NULL;
	}
	ebitmap_destroy(attr->types);
	free(attr->types);
	free(attr);
}

int cil_gen_bool(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, int tunableif)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_bool *boolean = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_bool_init(&boolean);

	key = parse_current->next->data;

	if (parse_current->next->next->data == CIL_KEY_CONDTRUE) {
		boolean->value = CIL_TRUE;
	} else if (parse_current->next->next->data == CIL_KEY_CONDFALSE) {
		boolean->value = CIL_FALSE;
	} else {
		cil_log(CIL_ERR, "Value must be either \'true\' or \'false\'");
		rc = SEPOL_ERR;
		goto exit;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)boolean, (hashtab_key_t)key, CIL_SYM_BOOLS, CIL_BOOL);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	if (tunableif) {
		cil_log(CIL_ERR, "Bad tunable (treated as a boolean due to preserve-tunables) declaration at line %d of %s\n",
			parse_current->line, parse_current->path);
	} else {
		cil_log(CIL_ERR, "Bad boolean declaration at line %d of %s\n",
			parse_current->line, parse_current->path);
	}
	cil_destroy_bool(boolean);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_bool(struct cil_bool *boolean)
{
	if (boolean == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&boolean->datum);
	free(boolean);
}

int cil_gen_tunable(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_tunable *tunable = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_tunable_init(&tunable);

	key = parse_current->next->data;

	if (parse_current->next->next->data == CIL_KEY_CONDTRUE) {
		tunable->value = CIL_TRUE;
	} else if (parse_current->next->next->data == CIL_KEY_CONDFALSE) {
		tunable->value = CIL_FALSE;
	} else {
		cil_log(CIL_ERR, "Value must be either \'true\' or \'false\'");
		rc = SEPOL_ERR;
		goto exit;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)tunable, (hashtab_key_t)key, CIL_SYM_TUNABLES, CIL_TUNABLE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad tunable declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_tunable(tunable);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_tunable(struct cil_tunable *tunable)
{
	if (tunable == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&tunable->datum);
	free(tunable);
}

static enum cil_flavor __cil_get_expr_operator_flavor(const char *op)
{
	if (op == NULL) return CIL_NONE;
	else if (op == CIL_KEY_AND)   return CIL_AND;
	else if (op == CIL_KEY_OR)    return CIL_OR;
	else if (op == CIL_KEY_NOT)   return CIL_NOT;
	else if (op == CIL_KEY_EQ)    return CIL_EQ;    /* Only conditional */
	else if (op == CIL_KEY_NEQ)   return CIL_NEQ;   /* Only conditional */
	else if (op == CIL_KEY_XOR)   return CIL_XOR;
	else if (op == CIL_KEY_ALL)   return CIL_ALL;   /* Only set and permissionx */
	else if (op == CIL_KEY_RANGE) return CIL_RANGE; /* Only catset and permissionx */
	else return CIL_NONE;
}

static int __cil_fill_expr(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list *expr, int *depth);

static int __cil_fill_expr_helper(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list *expr, int *depth)
{
	int rc = SEPOL_ERR;
	enum cil_flavor op;

	if (flavor == CIL_BOOL && *depth > COND_EXPR_MAXDEPTH) {
		cil_log(CIL_ERR, "Max depth of %d exceeded for boolean expression\n", COND_EXPR_MAXDEPTH);
		goto exit;
	}

	op = __cil_get_expr_operator_flavor(current->data);

	rc = cil_verify_expr_syntax(current, op, flavor);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (op != CIL_NONE) {
		cil_list_append(expr, CIL_OP, (void *)op);
		current = current->next;
	}

	if (op == CIL_NONE || op == CIL_ALL) {
		(*depth)++;
	}

	for (;current != NULL; current = current->next) {
		rc = __cil_fill_expr(current, flavor, expr, depth);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	(*depth)--;

	return SEPOL_OK;

exit:
	return rc;
}

static int __cil_fill_expr(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list *expr, int *depth)
{
	int rc = SEPOL_ERR;

	if (current->cl_head == NULL) {
		enum cil_flavor op = __cil_get_expr_operator_flavor(current->data);
		if (op != CIL_NONE) {
			cil_log(CIL_ERR, "Operator (%s) not in an expression\n", (char*)current->data);
			goto exit;
		}
		cil_list_append(expr, CIL_STRING, current->data);
	} else {
		struct cil_list *sub_expr;
		cil_list_init(&sub_expr, flavor);
		rc = __cil_fill_expr_helper(current->cl_head, flavor, sub_expr, depth);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_append(expr, CIL_LIST, sub_expr);
	}

	return SEPOL_OK;

exit:
	return rc;
}


int cil_gen_expr(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list **expr)
{
	int rc = SEPOL_ERR;
	int depth = 0;

	cil_list_init(expr, flavor);

	if (current->cl_head == NULL) {
		rc = __cil_fill_expr(current, flavor, *expr, &depth);
	} else {
		rc = __cil_fill_expr_helper(current->cl_head, flavor, *expr, &depth);
	}

	if (rc != SEPOL_OK) {
		cil_list_destroy(expr, CIL_TRUE);
		cil_log(CIL_ERR, "Bad expression\n");
	}

	return rc;
}

static enum cil_flavor __cil_get_constraint_operator_flavor(const char *op)
{
	if (op == CIL_KEY_AND)         return CIL_AND;
	else if (op == CIL_KEY_OR)     return CIL_OR;
	else if (op == CIL_KEY_NOT)    return CIL_NOT;
	else if (op == CIL_KEY_EQ)     return CIL_EQ;
	else if (op == CIL_KEY_NEQ)    return CIL_NEQ;
	else if (op == CIL_KEY_CONS_DOM)    return CIL_CONS_DOM;
	else if (op == CIL_KEY_CONS_DOMBY)  return CIL_CONS_DOMBY;
	else if (op == CIL_KEY_CONS_INCOMP) return CIL_CONS_INCOMP;
	else return CIL_NONE;
}

static enum cil_flavor __cil_get_constraint_operand_flavor(const char *operand)
{
	if (operand == NULL) return CIL_LIST;
	else if (operand == CIL_KEY_CONS_T1) return CIL_CONS_T1;
	else if (operand == CIL_KEY_CONS_T2) return CIL_CONS_T2;
	else if (operand == CIL_KEY_CONS_T3) return CIL_CONS_T3;
	else if (operand == CIL_KEY_CONS_R1) return CIL_CONS_R1;
	else if (operand == CIL_KEY_CONS_R2) return CIL_CONS_R2;
	else if (operand == CIL_KEY_CONS_R3) return CIL_CONS_R3;
	else if (operand == CIL_KEY_CONS_U1) return CIL_CONS_U1;
	else if (operand == CIL_KEY_CONS_U2) return CIL_CONS_U2;
	else if (operand == CIL_KEY_CONS_U3) return CIL_CONS_U3;
	else if (operand == CIL_KEY_CONS_L1) return CIL_CONS_L1;
	else if (operand == CIL_KEY_CONS_L2) return CIL_CONS_L2;
	else if (operand == CIL_KEY_CONS_H1) return CIL_CONS_H1;
	else if (operand == CIL_KEY_CONS_H2) return CIL_CONS_H2;
	else return CIL_STRING;
}

static int __cil_fill_constraint_leaf_expr(struct cil_tree_node *current, enum cil_flavor expr_flavor, enum cil_flavor op, struct cil_list **leaf_expr)
{
	int rc = SEPOL_ERR;
	enum cil_flavor leaf_expr_flavor = CIL_NONE;
	enum cil_flavor l_flavor = CIL_NONE;
	enum cil_flavor r_flavor = CIL_NONE;

	l_flavor = __cil_get_constraint_operand_flavor(current->next->data);
	r_flavor = __cil_get_constraint_operand_flavor(current->next->next->data);

	switch (l_flavor) {
	case CIL_CONS_U1:
	case CIL_CONS_U2:
	case CIL_CONS_U3:
		leaf_expr_flavor = CIL_USER;
		break;
	case CIL_CONS_R1:
	case CIL_CONS_R2:
	case CIL_CONS_R3:
		leaf_expr_flavor = CIL_ROLE;
		break;
	case CIL_CONS_T1:
	case CIL_CONS_T2:
	case CIL_CONS_T3:
		leaf_expr_flavor = CIL_TYPE;
		break;
	case CIL_CONS_L1:
	case CIL_CONS_L2:
	case CIL_CONS_H1:
	case CIL_CONS_H2:
		leaf_expr_flavor = CIL_LEVEL;
		break;
	default:
		cil_log(CIL_ERR, "Invalid left operand (%s)\n", (char*)current->next->data);
		goto exit;
	}

	rc = cil_verify_constraint_leaf_expr_syntax(l_flavor, r_flavor, op, expr_flavor);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_list_init(leaf_expr, leaf_expr_flavor);

	cil_list_append(*leaf_expr, CIL_OP, (void *)op);

	cil_list_append(*leaf_expr, CIL_CONS_OPERAND, (void *)l_flavor);

	if (r_flavor == CIL_STRING) {
		cil_list_append(*leaf_expr, CIL_STRING, current->next->next->data);
	} else if (r_flavor == CIL_LIST) {
		struct cil_list *sub_list;
		cil_fill_list(current->next->next->cl_head, leaf_expr_flavor, &sub_list);
		cil_list_append(*leaf_expr, CIL_LIST, &sub_list);
	} else {
		cil_list_append(*leaf_expr, CIL_CONS_OPERAND, (void *)r_flavor);
	}

	return SEPOL_OK;

exit:

	return SEPOL_ERR;
}

static int __cil_fill_constraint_expr(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list **expr, int *depth)
{
	int rc = SEPOL_ERR;
	enum cil_flavor op;
	struct cil_list *lexpr;
	struct cil_list *rexpr;

	if (current->data == NULL || current->cl_head != NULL) {
		cil_log(CIL_ERR, "Expected a string at the start of the constraint expression\n");
		goto exit;
	}

	if (*depth > CEXPR_MAXDEPTH) {
		cil_log(CIL_ERR, "Max depth of %d exceeded for constraint expression\n", CEXPR_MAXDEPTH);
		rc = SEPOL_ERR;
		goto exit;
	}

	op = __cil_get_constraint_operator_flavor(current->data);

	rc = cil_verify_constraint_expr_syntax(current, op);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	switch (op) {
	case CIL_EQ:
	case CIL_NEQ:
	case CIL_CONS_DOM:
	case CIL_CONS_DOMBY:
	case CIL_CONS_INCOMP:
		(*depth)++;
		rc = __cil_fill_constraint_leaf_expr(current, flavor, op, expr);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		break;
	case CIL_NOT:
		rc = __cil_fill_constraint_expr(current->next->cl_head, flavor, &lexpr, depth);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_init(expr, flavor);
		cil_list_append(*expr, CIL_OP, (void *)op);
		cil_list_append(*expr, CIL_LIST, lexpr);
		break;
	default:
		rc = __cil_fill_constraint_expr(current->next->cl_head, flavor, &lexpr, depth);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		rc = __cil_fill_constraint_expr(current->next->next->cl_head, flavor, &rexpr, depth);
		if (rc != SEPOL_OK) {
			goto exit;
		}
		cil_list_init(expr, flavor);
		cil_list_append(*expr, CIL_OP, (void *)op);
		cil_list_append(*expr, CIL_LIST, lexpr);
		cil_list_append(*expr, CIL_LIST, rexpr);
		break;
	}

	(*depth)--;

	return SEPOL_OK;
exit:

	return rc;
}

int cil_gen_constraint_expr(struct cil_tree_node *current, enum cil_flavor flavor, struct cil_list **expr)
{
	int rc = SEPOL_ERR;
	int depth = 0;

	if (current->cl_head == NULL) {
		goto exit;
	}

	rc = __cil_fill_constraint_expr(current->cl_head, flavor, expr, &depth);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:

	cil_log(CIL_ERR, "Bad expression tree for constraint\n");
	return rc;
}

int cil_gen_boolif(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, int tunableif)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_LIST,
		CIL_SYN_LIST | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_booleanif *bif = NULL;
	struct cil_tree_node *next = NULL;
	struct cil_tree_node *cond = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_boolif_init(&bif);
	bif->preserved_tunable = tunableif;

	rc = cil_gen_expr(parse_current->next, CIL_BOOL, &bif->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cond = parse_current->next->next;

	/* Destroying expr tree after stack is created*/
	if (cond->cl_head->data != CIL_KEY_CONDTRUE &&
		cond->cl_head->data != CIL_KEY_CONDFALSE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Conditional neither true nor false\n");
		goto exit;
	}

	if (cond->next != NULL) {
		cond = cond->next;
		if (cond->cl_head->data != CIL_KEY_CONDTRUE &&
			cond->cl_head->data != CIL_KEY_CONDFALSE) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "Conditional neither true nor false\n");
			goto exit;
		}
	}


	next = parse_current->next->next;
	cil_tree_subtree_destroy(parse_current->next);
	parse_current->next = next;

	ast_node->flavor = CIL_BOOLEANIF;
	ast_node->data = bif;

	return SEPOL_OK;

exit:
	if (tunableif) {
		cil_log(CIL_ERR, "Bad tunableif (treated as a booleanif due to preserve-tunables) declaration at line %d of %s\n",
				parse_current->line, parse_current->path);
	} else {
		cil_log(CIL_ERR, "Bad booleanif declaration at line %d of %s\n",
				parse_current->line, parse_current->path);
	}
	cil_destroy_boolif(bif);
	return rc;
}

void cil_destroy_boolif(struct cil_booleanif *bif)
{
	if (bif == NULL) {
		return;
	}

	cil_list_destroy(&bif->str_expr, CIL_TRUE);
	cil_list_destroy(&bif->datum_expr, CIL_FALSE);

	free(bif);
}

int cil_gen_tunif(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_LIST,
		CIL_SYN_LIST | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_tunableif *tif = NULL;
	struct cil_tree_node *next = NULL;
	struct cil_tree_node *cond = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_tunif_init(&tif);

	rc = cil_gen_expr(parse_current->next, CIL_TUNABLE, &tif->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cond = parse_current->next->next;

	if (cond->cl_head->data != CIL_KEY_CONDTRUE &&
		cond->cl_head->data != CIL_KEY_CONDFALSE) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Conditional neither true nor false\n");
		goto exit;
	}

	if (cond->next != NULL) {
		cond = cond->next;

		if (cond->cl_head->data != CIL_KEY_CONDTRUE &&
			cond->cl_head->data != CIL_KEY_CONDFALSE) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "Conditional neither true nor false\n");
			goto exit;
		}
	}

	/* Destroying expr tree after stack is created*/
	next = parse_current->next->next;
	cil_tree_subtree_destroy(parse_current->next);
	parse_current->next = next;

	ast_node->flavor = CIL_TUNABLEIF;
	ast_node->data = tif;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad tunableif declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_tunif(tif);
	return rc;
}

void cil_destroy_tunif(struct cil_tunableif *tif)
{
	if (tif == NULL) {
		return;
	}

	cil_list_destroy(&tif->str_expr, CIL_TRUE);
	cil_list_destroy(&tif->datum_expr, CIL_FALSE);

	free(tif);
}

int cil_gen_condblock(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_N_LISTS,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_condblock *cb = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (ast_node->parent->flavor != CIL_BOOLEANIF && ast_node->parent->flavor != CIL_TUNABLEIF) {
		rc = SEPOL_ERR;
		cil_log(CIL_ERR, "Conditional statements must be a direct child of a tunableif or booleanif statement.\n");
		goto exit;
	}

	ast_node->flavor = CIL_CONDBLOCK;

	cil_condblock_init(&cb);
	cb->flavor = flavor;

	ast_node->data = cb;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad %s condition declaration at line %d of %s\n",
		(char*)parse_current->data, parse_current->line, parse_current->path);
	cil_destroy_condblock(cb);
	return rc;
}

void cil_destroy_condblock(struct cil_condblock *cb)
{
	if (cb == NULL) {
		return;
	}

	cil_symtab_array_destroy(cb->symtab);
	free(cb);
}

int cil_gen_alias(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_alias *alias = NULL;
	enum cil_sym_index sym_index;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (flavor == CIL_TYPEALIAS && parse_current->next->data == CIL_KEY_SELF) {
		cil_log(CIL_ERR, "The keyword '%s' is reserved\n", CIL_KEY_SELF);
		rc = SEPOL_ERR;
		goto exit;
	}

	cil_alias_init(&alias);

	key = parse_current->next->data;

	rc = cil_flavor_to_symtab_index(flavor, &sym_index);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)alias, (hashtab_key_t)key, sym_index, flavor);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad %s declaration at line %d of %s\n",
		(char*)parse_current->data, parse_current->line, parse_current->path);
	cil_destroy_alias(alias);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_alias(struct cil_alias *alias)
{
	if (alias == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&alias->datum);
	alias->actual = NULL;

	free(alias);
}

int cil_gen_aliasactual(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_aliasactual *aliasactual = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if ((flavor == CIL_TYPEALIAS && parse_current->next->data == CIL_KEY_SELF) || parse_current->next->next->data == CIL_KEY_SELF) {
		cil_log(CIL_ERR, "The keyword '%s' is reserved\n", CIL_KEY_SELF);
		rc = SEPOL_ERR;
		goto exit;
	}

	cil_aliasactual_init(&aliasactual);

	aliasactual->alias_str = parse_current->next->data;

	aliasactual->actual_str = parse_current->next->next->data;

	ast_node->data = aliasactual;
	ast_node->flavor = flavor;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad %s association at line %d of %s\n", 
			cil_node_to_string(parse_current),parse_current->line, parse_current->path);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_aliasactual(struct cil_aliasactual *aliasactual)
{
	if (aliasactual == NULL) {
		return;
	}

	free(aliasactual);
}

int cil_gen_typeattributeset(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_typeattributeset *attrset = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_typeattributeset_init(&attrset);

	attrset->attr_str = parse_current->next->data;

	rc = cil_gen_expr(parse_current->next->next, CIL_TYPE, &attrset->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}
	ast_node->data = attrset;
	ast_node->flavor = CIL_TYPEATTRIBUTESET;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad typeattributeset statement at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_typeattributeset(attrset);
	return rc;
}

void cil_destroy_typeattributeset(struct cil_typeattributeset *attrset)
{
	if (attrset == NULL) {
		return;
	}

	cil_list_destroy(&attrset->str_expr, CIL_TRUE);
	cil_list_destroy(&attrset->datum_expr, CIL_FALSE);

	free(attrset);
}

int cil_gen_typepermissive(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_typepermissive *typeperm = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_typepermissive_init(&typeperm);

	typeperm->type_str = parse_current->next->data;

	ast_node->data = typeperm;
	ast_node->flavor = CIL_TYPEPERMISSIVE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad typepermissive declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_typepermissive(typeperm);
	return rc;
}

void cil_destroy_typepermissive(struct cil_typepermissive *typeperm)
{
	if (typeperm == NULL) {
		return;
	}

	free(typeperm);
}

int cil_gen_typetransition(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *s1, *s2, *s3, *s4, *s5;

	if (db == NULL || parse_current == NULL || ast_node == NULL ) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	s1 = parse_current->next->data;
	s2 = parse_current->next->next->data;
	s3 = parse_current->next->next->next->data;
	s4 = parse_current->next->next->next->next->data;
	s5 = NULL;

	if (parse_current->next->next->next->next->next) {
		if (s4 == CIL_KEY_STAR) {
			s4 = parse_current->next->next->next->next->next->data;
		} else {
			s5 = parse_current->next->next->next->next->next->data;
		}
	}

	if (s5) {
		struct cil_nametypetransition *nametypetrans = NULL;

		cil_nametypetransition_init(&nametypetrans);

		nametypetrans->src_str = s1;
		nametypetrans->tgt_str = s2;
		nametypetrans->obj_str = s3;
		nametypetrans->result_str = s5;
		nametypetrans->name_str = s4;

		ast_node->data = nametypetrans;
		ast_node->flavor = CIL_NAMETYPETRANSITION;
	} else {
		struct cil_type_rule *rule = NULL;

		cil_type_rule_init(&rule);

		rule->rule_kind = CIL_TYPE_TRANSITION;
		rule->src_str = s1;
		rule->tgt_str = s2;
		rule->obj_str = s3;
		rule->result_str = s4;

		ast_node->data = rule;
		ast_node->flavor = CIL_TYPE_RULE;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad typetransition declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	return rc;
}

void cil_destroy_name(struct cil_name *name)
{
	if (name == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&name->datum);
	free(name);
}

void cil_destroy_typetransition(struct cil_nametypetransition *nametypetrans)
{
	if (nametypetrans == NULL) {
		return;
	}

	free(nametypetrans);
}

int cil_gen_rangetransition(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_rangetransition *rangetrans = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL ) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_rangetransition_init(&rangetrans);

	rangetrans->src_str = parse_current->next->data;
	rangetrans->exec_str = parse_current->next->next->data;
	rangetrans->obj_str = parse_current->next->next->next->data;

	rangetrans->range_str = NULL;

	if (parse_current->next->next->next->next->cl_head == NULL) {
		rangetrans->range_str = parse_current->next->next->next->next->data;
	} else {
		cil_levelrange_init(&rangetrans->range);

		rc = cil_fill_levelrange(parse_current->next->next->next->next->cl_head, rangetrans->range);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = rangetrans;
	ast_node->flavor = CIL_RANGETRANSITION;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad rangetransition declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_rangetransition(rangetrans);
	return rc;
}

void cil_destroy_rangetransition(struct cil_rangetransition *rangetrans)
{
	if (rangetrans == NULL) {
		return;
	}

	if (rangetrans->range_str == NULL && rangetrans->range != NULL) {
		cil_destroy_levelrange(rangetrans->range);
	}

	free(rangetrans);
}

int cil_gen_sensitivity(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_sens *sens = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_sens_init(&sens);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)sens, (hashtab_key_t)key, CIL_SYM_SENS, CIL_SENS);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sensitivity declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_sensitivity(sens);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_sensitivity(struct cil_sens *sens)
{
	if (sens == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&sens->datum);

	cil_list_destroy(&sens->cats_list, CIL_FALSE);

	free(sens);
}

int cil_gen_category(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_cat *cat = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_cat_init(&cat);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)cat, (hashtab_key_t)key, CIL_SYM_CATS, CIL_CAT);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad category declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_category(cat);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_category(struct cil_cat *cat)
{
	if (cat == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&cat->datum);
	free(cat);
}

int cil_gen_catset(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_catset *catset = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_catset_init(&catset);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)catset, (hashtab_key_t)key, CIL_SYM_CATS, CIL_CATSET);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_fill_cats(parse_current->next->next, &catset->cats);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad categoryset declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_catset(catset);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_catset(struct cil_catset *catset)
{
	if (catset == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&catset->datum);

	cil_destroy_cats(catset->cats);

	free(catset);
}

int cil_gen_catorder(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_catorder *catorder = NULL;
	struct cil_list_item *curr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc !=  SEPOL_OK) {
		goto exit;
	}

	cil_catorder_init(&catorder);

	rc = cil_fill_list(parse_current->next->cl_head, CIL_CATORDER, &catorder->cat_list_str);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_list_for_each(curr, catorder->cat_list_str) {
		if (curr->data == CIL_KEY_UNORDERED) {
			cil_log(CIL_ERR, "Category order cannot be unordered.\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	ast_node->data = catorder;
	ast_node->flavor = CIL_CATORDER;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad categoryorder declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_catorder(catorder);
	return rc;
}

void cil_destroy_catorder(struct cil_catorder *catorder)
{
	if (catorder == NULL) {
		return;
	}

	if (catorder->cat_list_str != NULL) {
		cil_list_destroy(&catorder->cat_list_str, 1);
	}

	free(catorder);
}

int cil_gen_sensitivityorder(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_sensorder *sensorder = NULL;
	struct cil_list_item *curr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_sensorder_init(&sensorder);

	rc = cil_fill_list(parse_current->next->cl_head, CIL_SENSITIVITYORDER, &sensorder->sens_list_str);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_list_for_each(curr, sensorder->sens_list_str) {
		if (curr->data == CIL_KEY_UNORDERED) {
			cil_log(CIL_ERR, "Sensitivy order cannot be unordered.\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	}

	ast_node->data = sensorder;
	ast_node->flavor = CIL_SENSITIVITYORDER;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sensitivityorder declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_sensitivityorder(sensorder);
	return rc;
}

void cil_destroy_sensitivityorder(struct cil_sensorder *sensorder)
{
	if (sensorder == NULL) {
		return;
	}

	if (sensorder->sens_list_str != NULL) {
		cil_list_destroy(&sensorder->sens_list_str, CIL_TRUE);
	}

	free(sensorder);
}

int cil_gen_senscat(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_senscat *senscat = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_senscat_init(&senscat);

	senscat->sens_str = parse_current->next->data;

	rc = cil_fill_cats(parse_current->next->next, &senscat->cats);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = senscat;
	ast_node->flavor = CIL_SENSCAT;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad sensitivitycategory declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_senscat(senscat);
	return rc;
}

void cil_destroy_senscat(struct cil_senscat *senscat)
{
	if (senscat == NULL) {
		return;
	}

	cil_destroy_cats(senscat->cats);

	free(senscat);
}

int cil_gen_level(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_level *level = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_level_init(&level);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)level, (hashtab_key_t)key, CIL_SYM_LEVELS, CIL_LEVEL);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_fill_level(parse_current->next->next->cl_head, level);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad level declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_level(level);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_level(struct cil_level *level)
{
	if (level == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&level->datum);

	cil_destroy_cats(level->cats);

	free(level);
}

/* low should be pointing to either the name of the low level or to an open paren for an anonymous low level */
int cil_fill_levelrange(struct cil_tree_node *low, struct cil_levelrange *lvlrange)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;

	if (low == NULL || lvlrange == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(low, syntax, syntax_len);
	if (rc != SEPOL_OK) {

		goto exit;
	}

	if (low->cl_head == NULL) {
		lvlrange->low_str = low->data;
	} else {
		cil_level_init(&lvlrange->low);
		rc = cil_fill_level(low->cl_head, lvlrange->low);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (low->next->cl_head == NULL) {
		lvlrange->high_str = low->next->data;
	} else {
		cil_level_init(&lvlrange->high);
		rc = cil_fill_level(low->next->cl_head, lvlrange->high);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad levelrange\n");
	return rc;
}

int cil_gen_levelrange(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_levelrange *lvlrange = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_levelrange_init(&lvlrange);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)lvlrange, (hashtab_key_t)key, CIL_SYM_LEVELRANGES, CIL_LEVELRANGE);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_fill_levelrange(parse_current->next->next->cl_head, lvlrange);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad levelrange declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_levelrange(lvlrange);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_levelrange(struct cil_levelrange *lvlrange)
{
	if (lvlrange == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&lvlrange->datum);

	if (lvlrange->low_str == NULL) {
		cil_destroy_level(lvlrange->low);
	}

	if (lvlrange->high_str == NULL) {
		cil_destroy_level(lvlrange->high);
	}

	free(lvlrange);
}

int cil_gen_constrain(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_constrain *cons = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_constrain_init(&cons);

	rc = cil_fill_classperms_list(parse_current->next, &cons->classperms);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_constraint_expr(parse_current->next->next, flavor, &cons->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = cons;
	ast_node->flavor = flavor;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad constrain declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_constrain(cons);
	return rc;
}

void cil_destroy_constrain(struct cil_constrain *cons)
{
	if (cons == NULL) {
		return;
	}

	cil_destroy_classperms_list(&cons->classperms);
	cil_list_destroy(&cons->str_expr, CIL_TRUE);
	cil_list_destroy(&cons->datum_expr, CIL_FALSE);

	free(cons);
}

int cil_gen_validatetrans(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_validatetrans *validtrans = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_validatetrans_init(&validtrans);

	validtrans->class_str = parse_current->next->data;

	rc = cil_gen_constraint_expr(parse_current->next->next, flavor, &validtrans->str_expr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	ast_node->data = validtrans;
	ast_node->flavor = flavor;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad validatetrans declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_validatetrans(validtrans);
	return rc;


}

void cil_destroy_validatetrans(struct cil_validatetrans *validtrans)
{
	if (validtrans == NULL) {
		return;
	}

	cil_list_destroy(&validtrans->str_expr, CIL_TRUE);
	cil_list_destroy(&validtrans->datum_expr, CIL_FALSE);

	free(validtrans);
}

/* Fills in context starting from user */
int cil_fill_context(struct cil_tree_node *user_node, struct cil_context *context)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;

	if (user_node == NULL || context == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(user_node, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	context->user_str = user_node->data;
	context->role_str = user_node->next->data;
	context->type_str = user_node->next->next->data;

	context->range_str = NULL;

	if (user_node->next->next->next->cl_head == NULL) {
		context->range_str = user_node->next->next->next->data;
	} else {
		cil_levelrange_init(&context->range);

		rc = cil_fill_levelrange(user_node->next->next->next->cl_head, context->range);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad context\n");
	return rc;
}

int cil_gen_context(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_context *context = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_context_init(&context);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)context, (hashtab_key_t)key, CIL_SYM_CONTEXTS, CIL_CONTEXT);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_fill_context(parse_current->next->next->cl_head, context);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad context declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_context(context);
	cil_clear_node(ast_node);
	return SEPOL_ERR;
}

void cil_destroy_context(struct cil_context *context)
{
	if (context == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&context->datum);;

	if (context->range_str == NULL && context->range != NULL) {
		cil_destroy_levelrange(context->range);
	}

	free(context);
}

int cil_gen_filecon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST | CIL_SYN_EMPTY_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_filecon *filecon = NULL;
	char *type = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	type = parse_current->next->next->data;
	cil_filecon_init(&filecon);

	filecon->path_str = parse_current->next->data;

	if (type == CIL_KEY_FILE) {
		filecon->type = CIL_FILECON_FILE;
	} else if (type == CIL_KEY_DIR) {
		filecon->type = CIL_FILECON_DIR;
	} else if (type == CIL_KEY_CHAR) {
		filecon->type = CIL_FILECON_CHAR;
	} else if (type == CIL_KEY_BLOCK) {
		filecon->type = CIL_FILECON_BLOCK;
	} else if (type == CIL_KEY_SOCKET) {
		filecon->type = CIL_FILECON_SOCKET;
	} else if (type == CIL_KEY_PIPE) {
		filecon->type = CIL_FILECON_PIPE;
	} else if (type == CIL_KEY_SYMLINK) {
		filecon->type = CIL_FILECON_SYMLINK;
	} else if (type == CIL_KEY_ANY) {
		filecon->type = CIL_FILECON_ANY;
	} else {
		cil_log(CIL_ERR, "Invalid file type\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	if (parse_current->next->next->next->cl_head == NULL) {
		filecon->context_str = parse_current->next->next->next->data;
	} else {
		if (parse_current->next->next->next->cl_head->next == NULL) {
			filecon->context = NULL;
		} else {
			cil_context_init(&filecon->context);

			rc = cil_fill_context(parse_current->next->next->next->cl_head, filecon->context);
			if (rc != SEPOL_OK) {
				goto exit;
			}
		}
	}

	ast_node->data = filecon;
	ast_node->flavor = CIL_FILECON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad filecon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_filecon(filecon);
	return rc;
}

//TODO: Should we be checking if the pointer is NULL when passed in?
void cil_destroy_filecon(struct cil_filecon *filecon)
{
	if (filecon == NULL) {
		return;
	}

	if (filecon->context_str == NULL && filecon->context != NULL) {
		cil_destroy_context(filecon->context);
	}

	free(filecon);
}

int cil_gen_portcon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_portcon *portcon = NULL;
	char *proto;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_portcon_init(&portcon);

	proto = parse_current->next->data;
	if (proto == CIL_KEY_UDP) {
		portcon->proto = CIL_PROTOCOL_UDP;
	} else if (proto == CIL_KEY_TCP) {
		portcon->proto = CIL_PROTOCOL_TCP;
	} else {
		cil_log(CIL_ERR, "Invalid protocol\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	if (parse_current->next->next->cl_head != NULL) {
		if (parse_current->next->next->cl_head->next != NULL
		&& parse_current->next->next->cl_head->next->next == NULL) {
			rc = cil_fill_integer(parse_current->next->next->cl_head, &portcon->port_low);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper port specified\n");
				goto exit;
			}
			rc = cil_fill_integer(parse_current->next->next->cl_head->next, &portcon->port_high);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper port specified\n");
				goto exit;
			}
		} else {
			cil_log(CIL_ERR, "Improper port range specified\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	} else {
		rc = cil_fill_integer(parse_current->next->next, &portcon->port_low);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Improper port specified\n");
			goto exit;
		}
		portcon->port_high = portcon->port_low;
	}

	if (parse_current->next->next->next->cl_head == NULL ) {
		portcon->context_str = parse_current->next->next->next->data;
	} else {
		cil_context_init(&portcon->context);

		rc = cil_fill_context(parse_current->next->next->next->cl_head, portcon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = portcon;
	ast_node->flavor = CIL_PORTCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad portcon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_portcon(portcon);
	return rc;
}

void cil_destroy_portcon(struct cil_portcon *portcon)
{
	if (portcon == NULL) {
		return;
	}

	if (portcon->context_str == NULL && portcon->context != NULL) {
		cil_destroy_context(portcon->context);
	}

	free(portcon);
}

int cil_gen_nodecon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_nodecon *nodecon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_nodecon_init(&nodecon);

	if (parse_current->next->cl_head == NULL ) {
		nodecon->addr_str = parse_current->next->data;
	} else {
		cil_ipaddr_init(&nodecon->addr);

		rc = cil_fill_ipaddr(parse_current->next->cl_head, nodecon->addr);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (parse_current->next->next->cl_head == NULL ) {
		nodecon->mask_str = parse_current->next->next->data;
	} else {
		cil_ipaddr_init(&nodecon->mask);

		rc = cil_fill_ipaddr(parse_current->next->next->cl_head, nodecon->mask);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (parse_current->next->next->next->cl_head == NULL ) {
		nodecon->context_str = parse_current->next->next->next->data;
	} else {
		cil_context_init(&nodecon->context);

		rc = cil_fill_context(parse_current->next->next->next->cl_head, nodecon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = nodecon;
	ast_node->flavor = CIL_NODECON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad nodecon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_nodecon(nodecon);
	return rc;
}

void cil_destroy_nodecon(struct cil_nodecon *nodecon)
{
	if (nodecon == NULL) {
		return;
	}

	if (nodecon->addr_str == NULL && nodecon->addr != NULL) {
		cil_destroy_ipaddr(nodecon->addr);
	}

	if (nodecon->mask_str == NULL && nodecon->mask != NULL) {
		cil_destroy_ipaddr(nodecon->mask);
	}

	if (nodecon->context_str == NULL && nodecon->context != NULL) {
		cil_destroy_context(nodecon->context);
	}

	free(nodecon);
}

int cil_gen_genfscon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_genfscon *genfscon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_genfscon_init(&genfscon);

	genfscon->fs_str = parse_current->next->data;
	genfscon->path_str = parse_current->next->next->data;

	if (parse_current->next->next->next->cl_head == NULL ) {
		genfscon->context_str = parse_current->next->next->next->data;
	} else {
		cil_context_init(&genfscon->context);

		rc = cil_fill_context(parse_current->next->next->next->cl_head, genfscon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = genfscon;
	ast_node->flavor = CIL_GENFSCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad genfscon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_genfscon(genfscon);
	return SEPOL_ERR;
}

void cil_destroy_genfscon(struct cil_genfscon *genfscon)
{
	if (genfscon == NULL) {
		return;
	}

	if (genfscon->context_str == NULL && genfscon->context != NULL) {
		cil_destroy_context(genfscon->context);
	}

	free(genfscon);
}


int cil_gen_netifcon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_netifcon *netifcon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_netifcon_init(&netifcon);

	netifcon->interface_str = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		netifcon->if_context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&netifcon->if_context);

		rc = cil_fill_context(parse_current->next->next->cl_head, netifcon->if_context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	if (parse_current->next->next->next->cl_head == NULL) {
		netifcon->packet_context_str = parse_current->next->next->next->data;
	} else {
		cil_context_init(&netifcon->packet_context);

		rc = cil_fill_context(parse_current->next->next->next->cl_head, netifcon->packet_context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = netifcon;
	ast_node->flavor = CIL_NETIFCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad netifcon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_netifcon(netifcon);
	return SEPOL_ERR;
}

void cil_destroy_netifcon(struct cil_netifcon *netifcon)
{
	if (netifcon == NULL) {
		return;
	}

	if (netifcon->if_context_str == NULL && netifcon->if_context != NULL) {
		cil_destroy_context(netifcon->if_context);
	}

	if (netifcon->packet_context_str == NULL && netifcon->packet_context != NULL) {
		cil_destroy_context(netifcon->packet_context);
	}

	free(netifcon);
}

int cil_gen_pirqcon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_pirqcon *pirqcon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_pirqcon_init(&pirqcon);

	rc = cil_fill_integer(parse_current->next, &pirqcon->pirq);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->next->cl_head == NULL) {
		pirqcon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&pirqcon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, pirqcon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = pirqcon;
	ast_node->flavor = CIL_PIRQCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad pirqcon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_pirqcon(pirqcon);
	return rc;
}

void cil_destroy_pirqcon(struct cil_pirqcon *pirqcon)
{
	if (pirqcon == NULL) {
		return;
	}

	if (pirqcon->context_str == NULL && pirqcon->context != NULL) {
		cil_destroy_context(pirqcon->context);
	}

	free(pirqcon);
}

int cil_gen_iomemcon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_iomemcon *iomemcon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_iomemcon_init(&iomemcon);

	if (parse_current->next->cl_head != NULL) {
		if (parse_current->next->cl_head->next != NULL &&
		    parse_current->next->cl_head->next->next == NULL) {
			rc = cil_fill_integer64(parse_current->next->cl_head, &iomemcon->iomem_low);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper iomem specified\n");
				goto exit;
			}
			rc = cil_fill_integer64(parse_current->next->cl_head->next, &iomemcon->iomem_high);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper iomem specified\n");
				goto exit;
			}
		} else {
			cil_log(CIL_ERR, "Improper iomem range specified\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	} else {
		rc = cil_fill_integer64(parse_current->next, &iomemcon->iomem_low);;
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Improper iomem specified\n");
			goto exit;
		}
		iomemcon->iomem_high = iomemcon->iomem_low;
	}

	if (parse_current->next->next->cl_head == NULL ) {
		iomemcon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&iomemcon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, iomemcon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = iomemcon;
	ast_node->flavor = CIL_IOMEMCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad iomemcon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_iomemcon(iomemcon);
	return rc;
}

void cil_destroy_iomemcon(struct cil_iomemcon *iomemcon)
{
	if (iomemcon == NULL) {
		return;
	}

	if (iomemcon->context_str == NULL && iomemcon->context != NULL) {
		cil_destroy_context(iomemcon->context);
	}

	free(iomemcon);
}

int cil_gen_ioportcon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_ioportcon *ioportcon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_ioportcon_init(&ioportcon);

	if (parse_current->next->cl_head != NULL) {
		if (parse_current->next->cl_head->next != NULL &&
		    parse_current->next->cl_head->next->next == NULL) {
			rc = cil_fill_integer(parse_current->next->cl_head, &ioportcon->ioport_low);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper ioport specified\n");
				goto exit;
			}
			rc = cil_fill_integer(parse_current->next->cl_head->next, &ioportcon->ioport_high);
			if (rc != SEPOL_OK) {
				cil_log(CIL_ERR, "Improper ioport specified\n");
				goto exit;
			}
		} else {
			cil_log(CIL_ERR, "Improper ioport range specified\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	} else {
		rc = cil_fill_integer(parse_current->next, &ioportcon->ioport_low);
		if (rc != SEPOL_OK) {
			cil_log(CIL_ERR, "Improper ioport specified\n");
			goto exit;
		}
		ioportcon->ioport_high = ioportcon->ioport_low;
	}

	if (parse_current->next->next->cl_head == NULL ) {
		ioportcon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&ioportcon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, ioportcon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = ioportcon;
	ast_node->flavor = CIL_IOPORTCON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad ioportcon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_ioportcon(ioportcon);
	return rc;
}

void cil_destroy_ioportcon(struct cil_ioportcon *ioportcon)
{
	if (ioportcon == NULL) {
		return;
	}

	if (ioportcon->context_str == NULL && ioportcon->context != NULL) {
		cil_destroy_context(ioportcon->context);
	}

	free(ioportcon);
}

int cil_gen_pcidevicecon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_pcidevicecon *pcidevicecon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_pcidevicecon_init(&pcidevicecon);

	rc = cil_fill_integer(parse_current->next, &pcidevicecon->dev);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	if (parse_current->next->next->cl_head == NULL) {
		pcidevicecon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&pcidevicecon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, pcidevicecon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = pcidevicecon;
	ast_node->flavor = CIL_PCIDEVICECON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad pcidevicecon declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_pcidevicecon(pcidevicecon);
	return rc;
}

void cil_destroy_pcidevicecon(struct cil_pcidevicecon *pcidevicecon)
{
	if (pcidevicecon == NULL) {
		return;
	}

	if (pcidevicecon->context_str == NULL && pcidevicecon->context != NULL) {
		cil_destroy_context(pcidevicecon->context);
	}

	free(pcidevicecon);
}

int cil_gen_devicetreecon(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	int rc = SEPOL_ERR;
	struct cil_devicetreecon *devicetreecon = NULL;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_devicetreecon_init(&devicetreecon);

	devicetreecon->path = parse_current->next->data;

	if (parse_current->next->next->cl_head == NULL) {
		devicetreecon->context_str = parse_current->next->next->data;
	} else {
		cil_context_init(&devicetreecon->context);

		rc = cil_fill_context(parse_current->next->next->cl_head, devicetreecon->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = devicetreecon;
	ast_node->flavor = CIL_DEVICETREECON;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad devicetreecon declaration at line %d of %s\n",
		parse_current->line, parse_current->path);
	cil_destroy_devicetreecon(devicetreecon);
	return rc;
}

void cil_destroy_devicetreecon(struct cil_devicetreecon *devicetreecon)
{
	if (devicetreecon == NULL) {
		return;
	}

	if (devicetreecon->context_str == NULL && devicetreecon->context != NULL) {
		cil_destroy_context(devicetreecon->context);
	}

	free(devicetreecon);
}

int cil_gen_fsuse(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *type = NULL;
	struct cil_fsuse *fsuse = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	type = parse_current->next->data;

	cil_fsuse_init(&fsuse);

	if (type == CIL_KEY_XATTR) {
		fsuse->type = CIL_FSUSE_XATTR;
	} else if (type == CIL_KEY_TASK) {
		fsuse->type = CIL_FSUSE_TASK;
	} else if (type == CIL_KEY_TRANS) {
		fsuse->type = CIL_FSUSE_TRANS;
	} else {
		cil_log(CIL_ERR, "Invalid fsuse type\n");
		goto exit;
	}

	fsuse->fs_str = parse_current->next->next->data;

	if (parse_current->next->next->next->cl_head == NULL) {
		fsuse->context_str = parse_current->next->next->next->data;
	} else {
		cil_context_init(&fsuse->context);

		rc = cil_fill_context(parse_current->next->next->next->cl_head, fsuse->context);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	ast_node->data = fsuse;
	ast_node->flavor = CIL_FSUSE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad fsuse declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_fsuse(fsuse);
	return SEPOL_ERR;
}

void cil_destroy_fsuse(struct cil_fsuse *fsuse)
{
	if (fsuse == NULL) {
		return;
	}

	if (fsuse->context_str == NULL && fsuse->context != NULL) {
		cil_destroy_context(fsuse->context);
	}

	free(fsuse);
}

void cil_destroy_param(struct cil_param *param)
{
	if (param == NULL) {
		return;
	}

	free(param);
}

int cil_gen_macro(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	char *key = NULL;
	struct cil_macro *macro = NULL;
	struct cil_tree_node *macro_content = NULL;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST | CIL_SYN_EMPTY_LIST,
		CIL_SYN_N_LISTS | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/ sizeof(*syntax);

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc =__cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_macro_init(&macro);

	key = parse_current->next->data;

	struct cil_tree_node *current_item = parse_current->next->next->cl_head;
	while (current_item != NULL) {
		enum cil_syntax param_syntax[] = {
			CIL_SYN_STRING,
			CIL_SYN_STRING,
			CIL_SYN_END
		};
		int param_syntax_len = sizeof(param_syntax)/sizeof(*param_syntax);
		char *kind = NULL;
		struct cil_param *param = NULL;

		rc =__cil_verify_syntax(current_item->cl_head, param_syntax, param_syntax_len);
		if (rc != SEPOL_OK) {
			goto exit;
		}

		if (macro->params == NULL) {
			cil_list_init(&macro->params, CIL_LIST_ITEM);
		}

		kind = current_item->cl_head->data;
		cil_param_init(&param);

		if (kind == CIL_KEY_TYPE) {
			param->flavor = CIL_TYPE;
		} else if (kind == CIL_KEY_ROLE) {
			param->flavor = CIL_ROLE;
		} else if (kind == CIL_KEY_USER) {
			param->flavor = CIL_USER;
		} else if (kind == CIL_KEY_SENSITIVITY) {
			param->flavor = CIL_SENS;
		} else if (kind == CIL_KEY_CATEGORY) {
			param->flavor = CIL_CAT;
		} else if (kind == CIL_KEY_CATSET) {
			param->flavor = CIL_CATSET;
		} else if (kind == CIL_KEY_LEVEL) {
			param->flavor = CIL_LEVEL;
		} else if (kind == CIL_KEY_LEVELRANGE) {
			param->flavor = CIL_LEVELRANGE;
		} else if (kind == CIL_KEY_CLASS) {
			param->flavor = CIL_CLASS;
		} else if (kind == CIL_KEY_IPADDR) {
			param->flavor = CIL_IPADDR;
		} else if (kind == CIL_KEY_MAP_CLASS) {
			param->flavor = CIL_MAP_CLASS;
		} else if (kind == CIL_KEY_CLASSPERMISSION) {
			param->flavor = CIL_CLASSPERMISSION;
		} else if (kind == CIL_KEY_BOOL) {
			param->flavor = CIL_BOOL;
		} else if (kind == CIL_KEY_STRING) {
			param->flavor = CIL_NAME;
		} else if (kind == CIL_KEY_NAME) {
			param->flavor = CIL_NAME;
		} else {
			cil_log(CIL_ERR, "The kind %s is not allowed as a parameter\n",kind);
			cil_destroy_param(param);
			goto exit;
		}

		param->str =  current_item->cl_head->next->data;

		rc = __cil_verify_name(param->str);
		if (rc != SEPOL_OK) {
			cil_destroy_param(param);
			goto exit;
		}

		//walk current list and check for duplicate parameters
		struct cil_list_item *curr_param;
		cil_list_for_each(curr_param, macro->params) {
			if (param->str == ((struct cil_param*)curr_param->data)->str) {
				if (param->flavor == ((struct cil_param*)curr_param->data)->flavor) {
					cil_log(CIL_ERR, "Duplicate parameter\n");
					cil_destroy_param(param);
					goto exit;
				}
			}
		}

		cil_list_append(macro->params, CIL_PARAM, param);

		current_item = current_item->next;
	}

	/* we don't want the tree walker to walk the macro parameters (they were just handled above), so the subtree is deleted, and the next pointer of the
           node containing the macro name is updated to point to the start of the macro content */
	macro_content = parse_current->next->next->next;
	cil_tree_subtree_destroy(parse_current->next->next);
	parse_current->next->next = macro_content;
	if (macro_content == NULL) {
		/* No statements in macro and macro parameter list was last node */
		parse_current->parent->cl_tail = parse_current->next;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)macro, (hashtab_key_t)key, CIL_SYM_BLOCKS, CIL_MACRO);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad macro declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_macro(macro);
	cil_clear_node(ast_node);
	return SEPOL_ERR;
}

void cil_destroy_macro(struct cil_macro *macro)
{
	if (macro == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&macro->datum);
	cil_symtab_array_destroy(macro->symtab);

	if (macro->params != NULL) {
		cil_list_destroy(&macro->params, 1);
	}

	free(macro);
}

int cil_gen_call(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_LIST | CIL_SYN_EMPTY_LIST | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_call *call = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_call_init(&call);

	call->macro_str = parse_current->next->data;

	if (parse_current->next->next != NULL) {
		cil_tree_init(&call->args_tree);
		cil_copy_ast(db, parse_current->next->next, call->args_tree->root);
	}

	ast_node->data = call;
	ast_node->flavor = CIL_CALL;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad macro call at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_call(call);
	return rc;
}

void cil_destroy_call(struct cil_call *call)
{
	if (call == NULL) {
		return;
	}

	call->macro = NULL;

	if (call->args_tree != NULL) {
		cil_tree_destroy(&call->args_tree);
	}

	if (call->args != NULL) {
		cil_list_destroy(&call->args, 1);
	}

	free(call);
}

void cil_destroy_args(struct cil_args *args)
{
	if (args == NULL) {
		return;
	}

	if (args->arg_str != NULL) {
		args->arg_str = NULL;
	} else if (args->arg != NULL) {
		struct cil_tree_node *node = args->arg->nodes->head->data;
		switch (args->flavor) {
		case CIL_NAME:
			break;
		case CIL_CATSET:
			cil_destroy_catset((struct cil_catset *)args->arg);
			free(node);
			break;
		case CIL_LEVEL:
			cil_destroy_level((struct cil_level *)args->arg);
			free(node);
			break;
		case CIL_LEVELRANGE:
			cil_destroy_levelrange((struct cil_levelrange *)args->arg);
			free(node);
			break;
		case CIL_IPADDR:
			cil_destroy_ipaddr((struct cil_ipaddr *)args->arg);
			free(node);
			break;
		case CIL_CLASSPERMISSION:
			cil_destroy_classpermission((struct cil_classpermission *)args->arg);
			free(node);
			break;
		default:
			cil_log(CIL_ERR, "Destroying arg with the unexpected flavor=%d\n",args->flavor);
			break;
		}
	}

	args->param_str = NULL;
	args->arg = NULL;

	free(args);
}

int cil_gen_optional(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_N_LISTS | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_optional *optional = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_optional_init(&optional);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)optional, (hashtab_key_t)key, CIL_SYM_BLOCKS, CIL_OPTIONAL);
	if (rc != SEPOL_OK)
		goto exit;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad optional at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_optional(optional);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_optional(struct cil_optional *optional)
{
	if (optional == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&optional->datum);
	free(optional);
}

int cil_gen_policycap(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_policycap *polcap = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_policycap_init(&polcap);

	key = parse_current->next->data;

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)polcap, (hashtab_key_t)key, CIL_SYM_POLICYCAPS, CIL_POLICYCAP);
	if (rc != SEPOL_OK)
		goto exit;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad policycap statement at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_policycap(polcap);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_policycap(struct cil_policycap *polcap)
{
	if (polcap == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&polcap->datum);
	free(polcap);
}

int cil_gen_ipaddr(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	char *key = NULL;
	struct cil_ipaddr *ipaddr = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_ipaddr_init(&ipaddr);

	key  = parse_current->next->data;

	rc = cil_fill_ipaddr(parse_current->next->next, ipaddr);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	rc = cil_gen_node(db, ast_node, (struct cil_symtab_datum*)ipaddr, (hashtab_key_t)key, CIL_SYM_IPADDRS, CIL_IPADDR);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad ipaddr statement at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_ipaddr(ipaddr);
	cil_clear_node(ast_node);
	return rc;
}

void cil_destroy_ipaddr(struct cil_ipaddr *ipaddr)
{
	if (ipaddr == NULL) {
		return;
	}

	cil_symtab_datum_destroy(&ipaddr->datum);
	free(ipaddr);
}

int cil_fill_integer(struct cil_tree_node *int_node, uint32_t *integer)
{
	int rc = SEPOL_ERR;
	char *endptr = NULL;
	int val;

	if (int_node == NULL || integer == NULL) {
		goto exit;
	}

	errno = 0;
	val = strtol(int_node->data, &endptr, 10);
	if (errno != 0 || endptr == int_node->data || *endptr != '\0') {
		rc = SEPOL_ERR;
		goto exit;
	}

	*integer = val;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Failed to create integer from string\n");
	return rc;
}

int cil_fill_integer64(struct cil_tree_node *int_node, uint64_t *integer)
{
	int rc = SEPOL_ERR;
	char *endptr = NULL;
	uint64_t val;

	if (int_node == NULL || integer == NULL) {
		goto exit;
	}

	errno = 0;
	val = strtoull(int_node->data, &endptr, 10);
	if (errno != 0 || endptr == int_node->data || *endptr != '\0') {
		rc = SEPOL_ERR;
		goto exit;
	}

	*integer = val;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Failed to create integer from string\n");
	return rc;
}

int cil_fill_ipaddr(struct cil_tree_node *addr_node, struct cil_ipaddr *addr)
{
	int rc = SEPOL_ERR;

	if (addr_node == NULL || addr == NULL) {
		goto exit;
	}

	if (addr_node->cl_head != NULL ||  addr_node->next != NULL) {
		goto exit;
	}

	if (strchr(addr_node->data, '.') != NULL) {
		addr->family = AF_INET;
	} else {
		addr->family = AF_INET6;
	}

	rc = inet_pton(addr->family, addr_node->data, &addr->ip);
	if (rc != 1) {
		rc = SEPOL_ERR;
		goto exit;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad ip address or netmask\n"); 
	return rc;
}

int cil_fill_level(struct cil_tree_node *curr, struct cil_level *level)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST | CIL_SYN_END,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	if (curr == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(curr, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	level->sens_str = curr->data;
	if (curr->next != NULL) {
		rc = cil_fill_cats(curr->next, &level->cats);
		if (rc != SEPOL_OK) {
			goto exit;
		}
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad level\n");
	return rc;
}

int cil_fill_cats(struct cil_tree_node *curr, struct cil_cats **cats)
{
	int rc = SEPOL_ERR;

	cil_cats_init(cats);

	rc = cil_gen_expr(curr, CIL_CAT, &(*cats)->str_expr);
	if (rc != SEPOL_OK) {
		cil_destroy_cats(*cats);
	}

	return rc;
}

void cil_destroy_cats(struct cil_cats *cats)
{
	if (cats == NULL) {
		return;
	}

	cil_list_destroy(&cats->str_expr, CIL_TRUE);

	cil_list_destroy(&cats->datum_expr, CIL_FALSE);

	free(cats);
}
int cil_gen_bounds(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_bounds *bounds = NULL;
	int rc = SEPOL_ERR;

	if (db == NULL || parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_bounds_init(&bounds);

	bounds->parent_str = parse_current->next->data;
	bounds->child_str = parse_current->next->next->data;

	ast_node->data = bounds;

	switch (flavor) {
	case CIL_USER:
		ast_node->flavor = CIL_USERBOUNDS;
		break;
	case CIL_ROLE:
		ast_node->flavor = CIL_ROLEBOUNDS;
		break;
	case CIL_TYPE:
		ast_node->flavor = CIL_TYPEBOUNDS;
		break;
	default:
		break;
	}

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad bounds declaration at line %d of %s\n", 
		parse_current->line, parse_current->path);
	cil_destroy_bounds(bounds);
	return rc;
}

void cil_destroy_bounds(struct cil_bounds *bounds)
{
	if (bounds == NULL) {
		return;
	}

	free(bounds);
}

int cil_gen_default(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node, enum cil_flavor flavor)
{
	int rc = SEPOL_ERR;
	struct cil_default *def = NULL;
	char *object;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_default_init(&def);

	def->flavor = flavor;

	if (parse_current->next->cl_head == NULL) {
		cil_list_init(&def->class_strs, CIL_CLASS);
		cil_list_append(def->class_strs, CIL_STRING, parse_current->next->data);
		rc = SEPOL_OK;
	} else {
		rc = cil_fill_list(parse_current->next->cl_head, CIL_CLASS, &def->class_strs);
	}

	object = parse_current->next->next->data;
	if (object == CIL_KEY_SOURCE) {
		def->object = CIL_DEFAULT_SOURCE;
	} else if (object == CIL_KEY_TARGET) {
		def->object = CIL_DEFAULT_TARGET;
	} else {
		cil_log(CIL_ERR,"Expected either 'source' or 'target'\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	ast_node->data = def;
	ast_node->flavor = flavor;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad %s declaration at line %d of %s\n", 
			cil_node_to_string(parse_current), parse_current->line, parse_current->path);
	cil_destroy_default(def);
	return rc;
}

void cil_destroy_default(struct cil_default *def)
{
	if (def == NULL) {
		return;
	}

	cil_list_destroy(&def->class_strs, CIL_TRUE);

	cil_list_destroy(&def->class_datums, CIL_FALSE);

	free(def);
}

int cil_gen_defaultrange(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	struct cil_defaultrange *def = NULL;
	char *object;
	char *range;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING | CIL_SYN_LIST,
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_defaultrange_init(&def);

	if (parse_current->next->cl_head == NULL) {
		cil_list_init(&def->class_strs, CIL_CLASS);
		cil_list_append(def->class_strs, CIL_STRING, parse_current->next->data);
		rc = SEPOL_OK;
	} else {
		rc = cil_fill_list(parse_current->next->cl_head, CIL_CLASS, &def->class_strs);
	}

	object = parse_current->next->next->data;
	range = parse_current->next->next->next->data;
	if (object == CIL_KEY_SOURCE) {
		if (range == CIL_KEY_LOW) {
			def->object_range = CIL_DEFAULT_SOURCE_LOW;
		} else if (range == CIL_KEY_HIGH) {
			def->object_range = CIL_DEFAULT_SOURCE_HIGH;
		} else if (range == CIL_KEY_LOW_HIGH) {
			def->object_range = CIL_DEFAULT_SOURCE_LOW_HIGH;
		} else {
			cil_log(CIL_ERR,"Expected 'low', 'high', or 'low-high'\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	} else if (parse_current->next->next->data == CIL_KEY_TARGET) {
		if (range == CIL_KEY_LOW) {
			def->object_range = CIL_DEFAULT_TARGET_LOW;
		} else if (range == CIL_KEY_HIGH) {
			def->object_range = CIL_DEFAULT_TARGET_HIGH;
		} else if (range == CIL_KEY_LOW_HIGH) {
			def->object_range = CIL_DEFAULT_TARGET_LOW_HIGH;
		} else {
			cil_log(CIL_ERR,"Expected 'low', 'high', or 'low-high'\n");
			rc = SEPOL_ERR;
			goto exit;
		}
	} else {
		cil_log(CIL_ERR,"Expected either \'source\' or \'target\'\n");
		rc = SEPOL_ERR;
		goto exit;
	}

	ast_node->data = def;
	ast_node->flavor = CIL_DEFAULTRANGE;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad defaultrange declaration at line %d of %s\n", 
			parse_current->line, parse_current->path);
	cil_destroy_defaultrange(def);
	return rc;
}

void cil_destroy_defaultrange(struct cil_defaultrange *def)
{
	if (def == NULL) {
		return;
	}

	cil_list_destroy(&def->class_strs, CIL_TRUE);

	cil_list_destroy(&def->class_datums, CIL_FALSE);

	free(def);
}

int cil_gen_handleunknown(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_handleunknown *unknown = NULL;
	char *unknown_key;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_handleunknown_init(&unknown);

	unknown_key = parse_current->next->data;
	if (unknown_key == CIL_KEY_HANDLEUNKNOWN_ALLOW) {
		unknown->handle_unknown = SEPOL_ALLOW_UNKNOWN;
	} else if (unknown_key == CIL_KEY_HANDLEUNKNOWN_DENY) {
		unknown->handle_unknown = SEPOL_DENY_UNKNOWN;
	} else if (unknown_key == CIL_KEY_HANDLEUNKNOWN_REJECT) {
		unknown->handle_unknown = SEPOL_REJECT_UNKNOWN;
	} else {
		cil_log(CIL_ERR, "Expected either \'%s\', \'%s\', or \'%s\'\n", CIL_KEY_HANDLEUNKNOWN_ALLOW, CIL_KEY_HANDLEUNKNOWN_DENY, CIL_KEY_HANDLEUNKNOWN_REJECT);
		rc = SEPOL_ERR;
		goto exit;
	}

	ast_node->data = unknown;
	ast_node->flavor = CIL_HANDLEUNKNOWN;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad handleunknown at line %d of %s\n",
			parse_current->line, parse_current->path);
	cil_destroy_handleunknown(unknown);
	return rc;
}

void cil_destroy_handleunknown(struct cil_handleunknown *unk)
{
	free(unk);
}

int cil_gen_mls(struct cil_tree_node *parse_current, struct cil_tree_node *ast_node)
{
	int rc = SEPOL_ERR;
	enum cil_syntax syntax[] = {
		CIL_SYN_STRING,
		CIL_SYN_STRING,
		CIL_SYN_END
	};
	int syntax_len = sizeof(syntax)/sizeof(*syntax);
	struct cil_mls *mls = NULL;

	if (parse_current == NULL || ast_node == NULL) {
		goto exit;
	}

	rc = __cil_verify_syntax(parse_current, syntax, syntax_len);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	cil_mls_init(&mls);

	if (parse_current->next->data == CIL_KEY_CONDTRUE) {
		mls->value = CIL_TRUE;
	} else if (parse_current->next->data == CIL_KEY_CONDFALSE) {
		mls->value = CIL_FALSE;
	} else {
		cil_log(CIL_ERR, "Value must be either \'true\' or \'false\'");
		rc = SEPOL_ERR;
		goto exit;
	}

	ast_node->data = mls;
	ast_node->flavor = CIL_MLS;

	return SEPOL_OK;

exit:
	cil_log(CIL_ERR, "Bad mls at line %d of %s\n",
			parse_current->line, parse_current->path);
	cil_destroy_mls(mls);
	return rc;
}

void cil_destroy_mls(struct cil_mls *mls)
{
	free(mls);
}

int __cil_build_ast_node_helper(struct cil_tree_node *parse_current, uint32_t *finished, void *extra_args)
{
	struct cil_args_build *args = NULL;
	struct cil_tree_node *ast_current = NULL;
	struct cil_db *db = NULL;
	struct cil_tree_node *ast_node = NULL;
	struct cil_tree_node *macro = NULL;
	struct cil_tree_node *boolif = NULL;
	struct cil_tree_node *tunif = NULL;
	struct cil_tree_node *in = NULL;
	int rc = SEPOL_ERR;

	if (parse_current == NULL || finished == NULL || extra_args == NULL) {
		goto exit;
	}

	args = extra_args;
	ast_current = args->ast;
	db = args->db;
	macro = args->macro;
	boolif = args->boolif;
	tunif = args->tunif;
	in = args->in;

	if (parse_current->parent->cl_head != parse_current) {
		/* ignore anything that isn't following a parenthesis */
		rc = SEPOL_OK;
		goto exit;
	} else if (parse_current->data == NULL) {
		/* the only time parenthsis can immediately following parenthesis is if
		 * the parent is the root node */
		if (parse_current->parent->parent == NULL) {
			rc = SEPOL_OK;
		} else {
			cil_log(CIL_ERR, "Keyword expected after open parenthesis in line %d of %s\n", parse_current->line, parse_current->path);
		}
		goto exit;
	}

	if (macro != NULL) {
		if (parse_current->data == CIL_KEY_MACRO ||
			parse_current->data == CIL_KEY_TUNABLE ||
			parse_current->data == CIL_KEY_IN ||
			parse_current->data == CIL_KEY_BLOCK ||
			parse_current->data == CIL_KEY_BLOCKINHERIT ||
			parse_current->data == CIL_KEY_BLOCKABSTRACT) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "%s is not allowed in macros (%s:%d)\n", (char *)parse_current->data, parse_current->path, parse_current->line);
			goto exit;
		}
	}

	if (boolif != NULL) {
		if (parse_current->data != CIL_KEY_CONDTRUE &&
			parse_current->data != CIL_KEY_CONDFALSE &&
			parse_current->data != CIL_KEY_AUDITALLOW &&
			parse_current->data != CIL_KEY_TUNABLEIF &&
			parse_current->data != CIL_KEY_ALLOW &&
			parse_current->data != CIL_KEY_DONTAUDIT &&
			parse_current->data != CIL_KEY_TYPETRANSITION &&
			parse_current->data != CIL_KEY_TYPECHANGE &&
			parse_current->data != CIL_KEY_CALL) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "Found %s at line %d of %s\n",
				(char*)parse_current->data, parse_current->line, parse_current->path);
			if (((struct cil_booleanif*)boolif->data)->preserved_tunable) {
				cil_log(CIL_ERR, "%s cannot be defined within tunableif statement (treated as a booleanif due to preserve-tunables)\n",
						(char*)parse_current->data);
			} else {
				cil_log(CIL_ERR, "%s cannot be defined within booleanif statement\n",
						(char*)parse_current->data);
			}
			goto exit;
		}
	}

	if (tunif != NULL) {
		if (parse_current->data == CIL_KEY_TUNABLE) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "Found tunable at line %d of %s\n",
				parse_current->line, parse_current->path);
			cil_log(CIL_ERR, "Tunables cannot be defined within tunableif statement\n");
			goto exit;
		}
	}

	if (in != NULL) {
		if (parse_current->data == CIL_KEY_IN) {
			rc = SEPOL_ERR;
			cil_log(CIL_ERR, "Found in-statement at line %d of %s\n",
				parse_current->line, parse_current->path);
			cil_log(CIL_ERR, "in-statements cannot be defined within in-statements\n");
			goto exit;
		}
	}

	cil_tree_node_init(&ast_node);

	ast_node->parent = ast_current;
	ast_node->line = parse_current->line;
	ast_node->path = parse_current->path;

	if (parse_current->data == CIL_KEY_BLOCK) {
		rc = cil_gen_block(db, parse_current, ast_node, 0);
	} else if (parse_current->data == CIL_KEY_BLOCKINHERIT) {
		rc = cil_gen_blockinherit(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_BLOCKABSTRACT) {
		rc = cil_gen_blockabstract(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_IN) {
		rc = cil_gen_in(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_CLASS) {
		rc = cil_gen_class(db, parse_current, ast_node);
		// To avoid parsing list of perms again
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CLASSORDER) {
		rc = cil_gen_classorder(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_MAP_CLASS) {
		rc = cil_gen_map_class(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CLASSMAPPING) {
		rc = cil_gen_classmapping(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CLASSPERMISSION) {
		rc = cil_gen_classpermission(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CLASSPERMISSIONSET) {
		rc = cil_gen_classpermissionset(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_COMMON) {
		rc = cil_gen_common(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CLASSCOMMON) {
		rc = cil_gen_classcommon(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_SID) {
		rc = cil_gen_sid(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_SIDCONTEXT) {
		rc = cil_gen_sidcontext(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_SIDORDER) {
		rc = cil_gen_sidorder(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_USER) {
		rc = cil_gen_user(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_USERATTRIBUTE) {
		rc = cil_gen_userattribute(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_USERATTRIBUTESET) {
		rc = cil_gen_userattributeset(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_USERLEVEL) {
		rc = cil_gen_userlevel(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_USERRANGE) {
		rc = cil_gen_userrange(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_USERBOUNDS) {
		rc = cil_gen_bounds(db, parse_current, ast_node, CIL_USER);
	} else if (parse_current->data == CIL_KEY_USERPREFIX) {
		rc = cil_gen_userprefix(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_SELINUXUSER) {
		rc = cil_gen_selinuxuser(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_SELINUXUSERDEFAULT) {
		rc = cil_gen_selinuxuserdefault(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_TYPE) {
		rc = cil_gen_type(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_TYPEATTRIBUTE) {
		rc = cil_gen_typeattribute(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_TYPEATTRIBUTESET) {
		rc = cil_gen_typeattributeset(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_TYPEALIAS) {
		rc = cil_gen_alias(db, parse_current, ast_node, CIL_TYPEALIAS);
	} else if (parse_current->data == CIL_KEY_TYPEALIASACTUAL) {
		rc = cil_gen_aliasactual(db, parse_current, ast_node, CIL_TYPEALIASACTUAL);
	} else if (parse_current->data == CIL_KEY_TYPEBOUNDS) {
		rc = cil_gen_bounds(db, parse_current, ast_node, CIL_TYPE);
	} else if (parse_current->data == CIL_KEY_TYPEPERMISSIVE) {
		rc = cil_gen_typepermissive(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_RANGETRANSITION) {
		rc = cil_gen_rangetransition(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_ROLE) {
		rc = cil_gen_role(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_USERROLE) {
		rc = cil_gen_userrole(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_ROLETYPE) {
		rc = cil_gen_roletype(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_ROLETRANSITION) {
		rc = cil_gen_roletransition(parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_ROLEALLOW) {
		rc = cil_gen_roleallow(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_ROLEATTRIBUTE) {
		rc = cil_gen_roleattribute(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_ROLEATTRIBUTESET) {
		rc = cil_gen_roleattributeset(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_ROLEBOUNDS) {
		rc = cil_gen_bounds(db, parse_current, ast_node, CIL_ROLE);
	} else if (parse_current->data == CIL_KEY_BOOL) {
		rc = cil_gen_bool(db, parse_current, ast_node, CIL_FALSE);
	} else if (parse_current->data == CIL_KEY_BOOLEANIF) {
		rc = cil_gen_boolif(db, parse_current, ast_node, CIL_FALSE);
	} else if(parse_current->data == CIL_KEY_TUNABLE) {
		if (db->preserve_tunables) {
			rc = cil_gen_bool(db, parse_current, ast_node, CIL_TRUE);
		} else {
			rc = cil_gen_tunable(db, parse_current, ast_node);
		}
	} else if (parse_current->data == CIL_KEY_TUNABLEIF) {
		if (db->preserve_tunables) {
			rc = cil_gen_boolif(db, parse_current, ast_node, CIL_TRUE);
		} else {
			rc = cil_gen_tunif(db, parse_current, ast_node);
		}
	} else if (parse_current->data == CIL_KEY_CONDTRUE) {
		rc = cil_gen_condblock(db, parse_current, ast_node, CIL_CONDTRUE);
	} else if (parse_current->data == CIL_KEY_CONDFALSE) {
		rc = cil_gen_condblock(db, parse_current, ast_node, CIL_CONDFALSE);
	} else if (parse_current->data == CIL_KEY_ALLOW) {
		rc = cil_gen_avrule(parse_current, ast_node, CIL_AVRULE_ALLOWED);
		// So that the object and perms lists do not get parsed again
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_AUDITALLOW) {
		rc = cil_gen_avrule(parse_current, ast_node, CIL_AVRULE_AUDITALLOW);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DONTAUDIT) {
		rc = cil_gen_avrule(parse_current, ast_node, CIL_AVRULE_DONTAUDIT);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_NEVERALLOW) {
		rc = cil_gen_avrule(parse_current, ast_node, CIL_AVRULE_NEVERALLOW);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_ALLOWX) {
		rc = cil_gen_avrulex(parse_current, ast_node, CIL_AVRULE_ALLOWED);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_AUDITALLOWX) {
		rc = cil_gen_avrulex(parse_current, ast_node, CIL_AVRULE_AUDITALLOW);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DONTAUDITX) {
		rc = cil_gen_avrulex(parse_current, ast_node, CIL_AVRULE_DONTAUDIT);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_NEVERALLOWX) {
		rc = cil_gen_avrulex(parse_current, ast_node, CIL_AVRULE_NEVERALLOW);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_PERMISSIONX) {
		rc = cil_gen_permissionx(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_TYPETRANSITION) {
		rc = cil_gen_typetransition(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_TYPECHANGE) {
		rc = cil_gen_type_rule(parse_current, ast_node, CIL_TYPE_CHANGE);
	} else if (parse_current->data == CIL_KEY_TYPEMEMBER) {
		rc = cil_gen_type_rule(parse_current, ast_node, CIL_TYPE_MEMBER);
	} else if (parse_current->data == CIL_KEY_SENSITIVITY) {
		rc = cil_gen_sensitivity(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_SENSALIAS) {
		rc = cil_gen_alias(db, parse_current, ast_node, CIL_SENSALIAS);
	} else if (parse_current->data == CIL_KEY_SENSALIASACTUAL) {
		rc = cil_gen_aliasactual(db, parse_current, ast_node, CIL_SENSALIASACTUAL);
	} else if (parse_current->data == CIL_KEY_CATEGORY) {
		rc = cil_gen_category(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_CATALIAS) {
		rc = cil_gen_alias(db, parse_current, ast_node, CIL_CATALIAS);
	} else if (parse_current->data == CIL_KEY_CATALIASACTUAL) {
		rc = cil_gen_aliasactual(db, parse_current, ast_node, CIL_CATALIASACTUAL);
	} else if (parse_current->data == CIL_KEY_CATSET) {
		rc = cil_gen_catset(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CATORDER) {
		rc = cil_gen_catorder(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_SENSITIVITYORDER) {
		rc = cil_gen_sensitivityorder(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_SENSCAT) {
		rc = cil_gen_senscat(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_LEVEL) {
		rc = cil_gen_level(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_LEVELRANGE) {
		rc = cil_gen_levelrange(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CONSTRAIN) {
		rc = cil_gen_constrain(db, parse_current, ast_node, CIL_CONSTRAIN);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_MLSCONSTRAIN) {
		rc = cil_gen_constrain(db, parse_current, ast_node, CIL_MLSCONSTRAIN);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_VALIDATETRANS) {
		rc = cil_gen_validatetrans(db, parse_current, ast_node, CIL_VALIDATETRANS);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_MLSVALIDATETRANS) {
		rc = cil_gen_validatetrans(db, parse_current, ast_node, CIL_MLSVALIDATETRANS);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_CONTEXT) {
		rc = cil_gen_context(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_FILECON) {
		rc = cil_gen_filecon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_PORTCON) {
		rc = cil_gen_portcon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_NODECON) {
		rc = cil_gen_nodecon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_GENFSCON) {
		rc = cil_gen_genfscon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_NETIFCON) {
		rc = cil_gen_netifcon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_PIRQCON) {
		rc = cil_gen_pirqcon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_IOMEMCON) {
		rc = cil_gen_iomemcon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_IOPORTCON) {
		rc = cil_gen_ioportcon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_PCIDEVICECON) {
		rc = cil_gen_pcidevicecon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DEVICETREECON) {
		rc = cil_gen_devicetreecon(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_FSUSE) {
		rc = cil_gen_fsuse(db, parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_MACRO) {
		rc = cil_gen_macro(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_CALL) {
		rc = cil_gen_call(db, parse_current, ast_node);
		*finished = 1;
	} else if (parse_current->data == CIL_KEY_POLICYCAP) {
		rc = cil_gen_policycap(db, parse_current, ast_node);
		*finished = 1;
	} else if (parse_current->data == CIL_KEY_OPTIONAL) {
		rc = cil_gen_optional(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_IPADDR) {
		rc = cil_gen_ipaddr(db, parse_current, ast_node);
	} else if (parse_current->data == CIL_KEY_DEFAULTUSER) {
		rc = cil_gen_default(parse_current, ast_node, CIL_DEFAULTUSER);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DEFAULTROLE) {
		rc = cil_gen_default(parse_current, ast_node, CIL_DEFAULTROLE);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DEFAULTTYPE) {
		rc = cil_gen_default(parse_current, ast_node, CIL_DEFAULTTYPE);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_DEFAULTRANGE) {
		rc = cil_gen_defaultrange(parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_HANDLEUNKNOWN) {
		rc = cil_gen_handleunknown(parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else if (parse_current->data == CIL_KEY_MLS) {
		rc = cil_gen_mls(parse_current, ast_node);
		*finished = CIL_TREE_SKIP_NEXT;
	} else {
		cil_log(CIL_ERR, "Error: Unknown keyword %s\n", (char*)parse_current->data);
		rc = SEPOL_ERR;
	}

	if (rc == SEPOL_OK) {
		if (ast_current->cl_head == NULL) {
			if (ast_current->flavor == CIL_MACRO) {
				args->macro = ast_current;
			}

			if (ast_current->flavor == CIL_BOOLEANIF) {
				args->boolif = ast_current;
			}

			if (ast_current->flavor == CIL_TUNABLEIF) {
				args->tunif = ast_current;
			}

			if (ast_current->flavor == CIL_IN) {
				args->in = ast_current;
			}
		
			ast_current->cl_head = ast_node;
		} else {
			ast_current->cl_tail->next = ast_node;
		}
		ast_current->cl_tail = ast_node;
		ast_current = ast_node;
		args->ast = ast_current;
	} else {
		cil_tree_node_destroy(&ast_node);
	}

exit:
	return rc;
}

int __cil_build_ast_last_child_helper(struct cil_tree_node *parse_current, void *extra_args)
{
	int rc = SEPOL_ERR;
	struct cil_tree_node *ast = NULL;
	struct cil_args_build *args = NULL;

	if (extra_args == NULL) {
		goto exit;
	}

	args = extra_args;
	ast = args->ast;

	if (ast->flavor == CIL_ROOT) {
		rc = SEPOL_OK;
		goto exit;
	}

	args->ast = ast->parent;

	if (ast->flavor == CIL_MACRO) {
		args->macro = NULL;
	}

	if (ast->flavor == CIL_BOOLEANIF) {
		args->boolif = NULL;
	}

	if (ast->flavor == CIL_TUNABLEIF) {
		args->tunif = NULL;
	}

	if (ast->flavor == CIL_IN) {
		args->in = NULL;
	}

	// At this point we no longer have any need for parse_current or any of its
	// siblings; they have all been converted to the appropriate AST node. The
	// full parse tree will get deleted elsewhere, but in an attempt to
	// minimize memory useage (of which the parse tree uses alot), start
	// deleting the parts we don't need now.
	cil_tree_children_destroy(parse_current->parent);

	return SEPOL_OK;

exit:
	return rc;
}

int cil_build_ast(__attribute__((unused)) struct cil_db *db, struct cil_tree_node *parse_tree, struct cil_tree_node *ast)
{
	int rc = SEPOL_ERR;
	struct cil_args_build extra_args;

	if (db == NULL || parse_tree == NULL || ast == NULL) {
		goto exit;
	}

	extra_args.ast = ast;
	extra_args.db = db;
	extra_args.macro = NULL;
	extra_args.boolif = NULL;
	extra_args.tunif = NULL;
	extra_args.in = NULL;

	rc = cil_tree_walk(parse_tree, __cil_build_ast_node_helper, NULL, __cil_build_ast_last_child_helper, &extra_args);
	if (rc != SEPOL_OK) {
		goto exit;
	}

	return SEPOL_OK;

exit:
	return rc;
}
