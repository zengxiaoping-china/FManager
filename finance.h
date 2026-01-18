// finance.h
#ifndef FINANCE_H
#define FINANCE_H

void init_finance_database(void);
void add_record(void);
void list_records(void);
void edit_record(void);
void delete_record(void);
void export_to_csv(void);
void query_by_date(void);
void query_by_category(void);
void show_monthly_report(void);
void show_yearly_report(void);
void show_category_report(void);
int select_category(const char* type);
int select_account(void);
int select_member(void);

#endif