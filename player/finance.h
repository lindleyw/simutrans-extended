/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef PLAYER_FINANCE_H
#define PLAYER_FINANCE_H


#include <assert.h>

#include "../simtypes.h"
#include "../simworld.h"

 /****************************************** notification strings **************************************/

 /**
 * Translated notification text identifiers used by tools are placed here.
 * This is because they are not simple well structured internal identifiers.
 * Instead they can be complex sentences intended to be read untranslated.
 * Using these constants assues a valid and correct text identifier is choosen.
 */

 /**
 * Message returned when a player cannot afford to complete an action.
 */
char const *const NOTICE_INSUFFICIENT_FUNDS = "That would exceed\nyour credit limit.";

/**
* Message returned when a player tries to place trees when trees are disabled.
*/
char const *const NOTICE_NO_TREES = "Trees disabled!";

/**
* Message returned when valid terrain cannot be found for a tool to use.
*/
char const *const NOTICE_UNSUITABLE_GROUND = "No suitable ground!";

/**
* Message returned when a tool fails because the target is owned by another player and access is not granted.
*/
char const *const NOTICE_OWNED_BY_OTHER_PLAYER = "Das Feld gehoert\neinem anderen Spieler\n";

/**
* Message returned when a depot cannot be placed.
*/
char const *const NOTICE_DEPOT_BAD_POS = "Cannot built depot here!";

/**
* Message returned when a tool fails due to the target tile being occupied.
*/
char const *const NOTICE_TILE_FULL = "Tile not empty.";

/**
 * Message returned when a company tries to make a public way when public ways are disabled.
 */
char const *const NOTICE_DISABLED_PUBLIC_WAY = "Not allowed to make publicly owned ways!";

/// for compatibility with old versions
/// Must be different in extended!
#define OLD_MAX_PLAYER_COST (21)

/// number of years to keep history
#define MAX_PLAYER_HISTORY_YEARS  (26)

/// number of months to keep history
#define MAX_PLAYER_HISTORY_MONTHS (26)


/**
 * Type of transport used in accounting statistics.
 * waytype_t was not used because of values assigned to air_wt and powerline_wt.
 * There are also buildings like railway station that can be distinguished
 * by transport_type and can not be distinguished by waytype_t.
 */
enum transport_type {
	TT_ALL=0,
	TT_ROAD,
	TT_RAILWAY,
	TT_SHIP,
	TT_MONORAIL,
	TT_MAGLEV,
	TT_TRAM,
	TT_NARROWGAUGE,
	TT_AIR,
	TT_OTHER,  ///< everything else that can not be differentiated (e.g. houses), not powerlines
	TT_MAX_VEH = TT_OTHER,
	TT_POWERLINE,
	TT_MAX
};


/**
 * ATC = accounting type common (common means data common for all transport types).
 *
 * Supersedes COST_ types, that CAN NOT be distinguished by type of transport-
 * - the data are concerning to whole company
 */
enum accounting_type_common {
	ATC_CASH = 0,				///< Cash
	ATC_NETWEALTH,				///< Total Cash + Assets
	ATC_HALTS,			        ///< Halt count
	ATC_SCENARIO_COMPLETED,		///< Scenario success (only useful if there is one ... )
	ATC_INTEREST,				/// interest received/paid
	ATC_SOFT_CREDIT_LIMIT,		/// soft credit limit (player cannot spend money)
	ATC_HARD_CREDIT_LIMIT,		/// hard credit limit (player is insolvent)
	ATC_MAX
};


/**
 * ATV = accounting type vehicles.
 * Supersedes COST_ types, that CAN be distinguished by type of transport.
 */
enum accounting_type_vehicles {
	ATV_REVENUE_PASSENGER = 0, ///< Revenue from passenger transport
	ATV_REVENUE_MAIL,          ///< Revenue from mail transport
	ATV_REVENUE_GOOD,          ///< Revenue from good transport
	ATV_REVENUE_TRANSPORT,     ///< Operating profit = passenger + mail + goods = was: COST_INCOME
	ATV_TOLL_RECEIVED,         ///< Toll paid to you by another player
	ATV_REVENUE,               ///< Operating profit = revenue_transport + toll = passenger + mail+ goods + toll_received

	ATV_RUNNING_COST,               ///< Distance based running costs, was: COST_VEHICLE_RUN
	ATV_VEHICLE_MAINTENANCE,        ///< Monthly vehicle maintenance.
	ATV_INFRASTRUCTURE_MAINTENANCE, ///< Infrastructure maintenance (roads, railway, ...), was: COST_MAINTENANCE
	ATV_TOLL_PAID,                  ///< Toll paid by you to another player
	ATV_EXPENDITURE,                ///< Total expenditure = RUNNING_COSTS+VEHICLE_MAINTENANCE+INFRACTRUCTURE_MAINTENANCE+TOLL_PAID
	ATV_OPERATING_PROFIT,           ///< ATV_REVENUE - ATV_EXPENDITURE, was: COST_OPERATING_PROFIT
	ATV_NEW_VEHICLE,                ///< Payment for new vehicles
	ATV_CONSTRUCTION_COST,          ///< Construction cost, COST_CONSTRUCTION mapped here
	ATV_PROFIT,                     ///< ATV_OPERATING_PROFIT - (CONSTRUCTION_COST + NEW_VEHICLE) + COST_INTEREST, was: COST_PROFIT
	ATV_WAY_TOLL,                   ///< = ATV_TOLL_PAID + ATV_TOLL_RECEIVED, was: COST_WAY_TOLLS
	ATV_NON_FINANCIAL_ASSETS,       ///< Value of vehicles owned by your company, was: COST_ASSETS
	ATV_PROFIT_MARGIN,              ///< ATV_OPERATING_PROFIT / ATV_REVENUE, was: COST_MARGIN

	ATV_TRANSPORTED_PASSENGER, ///< Passenger-distance
	ATV_TRANSPORTED_MAIL,      ///< Number of transported mail
	ATV_TRANSPORTED_GOOD,      ///< Payload-distance in tonne km

	ATV_CONVOY_DISTANCE,       ///< Travel distance of convoys
	ATV_VEHICLE_DISTANCE,      ///< Travel distance of vehicles

	// == The records below must carry over the previous month's data to the new month.
	ATV_CONVOIS,               ///< Number of convois
	ATV_CARRY_OVER_DATA_TO_NEXT_MON = ATV_CONVOIS,
	ATV_VEHICLES,              ///< Number of vehicles
	ATV_WAY_LENGTH,            ///< tile base length for Way kilometreage

	ATV_MAX
};


class loadsave_t;
class karte_t;
class player_t;
class scenario_t;


/**
 * Encapsulate margin calculation  (Operating_Profit / Income)
 */
inline sint64 calc_margin(sint64 operating_profit, sint64 proceeds)
{
	if (proceeds == 0) {
		return 0;
	}
	return (10000 * operating_profit) / proceeds;
}


/**
 * convert to displayed value
 */
inline sint64 convert_money(sint64 value) { return (value + 50) / 100; }


/**
 * Class to encapsulate all company related statistics.
 */
class finance_t {
	/** transport company */
	player_t * player;

	karte_t * world;

	/**
	 * Amount of money, previously known as "konto"
	 */
	sint64 account_balance;

	/**
	 * Shows how many months you have been in red numbers.
	 */
	sint32 account_overdrawn;

	/**
	 * Remember the starting money, used e.g. in scenarios.
	 */
	sint64 starting_money;

	/**
	 * Contains values having relation with whole company but not with particular
	 * type of transport (com - common).
	 */
	sint64 com_year[MAX_PLAYER_HISTORY_YEARS][ATC_MAX];

	/**
	 * Monthly finance history, data not distinguishable by transport type.
	 */
	sint64 com_month[MAX_PLAYER_HISTORY_MONTHS][ATC_MAX];

	/**
	 * Finance history having relation with particular type of service
	 */
	sint64 veh_year[TT_MAX][MAX_PLAYER_HISTORY_YEARS][ATV_MAX];
	sint64 veh_month[TT_MAX][MAX_PLAYER_HISTORY_MONTHS][ATV_MAX];

	/**
	 * Monthly maintenance cost
	 */
	sint64 maintenance[TT_MAX];

	/**
	 * Monthly vehicle maintenance cost per transport type.
	 */
	// Unused because vehicle maintenance varies monthly for each vehicle.
	// sint64 vehicle_maintenance[TT_MAX];

public:
	finance_t(player_t * _player, karte_t * _world);

	/**
	 * Adds construction cost to finance stats.
	 * @param amount sum of money
	 * @param wt way type, e.g. tram_wt
	 */
	inline void book_construction_costs(const sint64 amount, const waytype_t wt) {
		transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_CONSTRUCTION_COST] += amount;
		veh_month[tt][0][ATV_CONSTRUCTION_COST] += amount;

		account_balance += amount;
	}

	/**
	 * Adds count to number of convois in statistics.
	 */
	inline void book_convoi_number( const int count, const waytype_t wt) {
		veh_year[TT_ALL][0][ATV_CONVOIS]  += count;
		veh_month[TT_ALL][0][ATV_CONVOIS] += count;
		transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_CONVOIS]  += count;
		veh_month[tt][0][ATV_CONVOIS] += count;
	}

	/**
	 * Adds count to number of stops in statistics.
	 */
	inline void book_stop_number( const int count ) {
		com_year[0][ATC_HALTS]  += count;
		com_month[0][ATC_HALTS] += count;
	}

	/**
	 * Adds maintenance into/from finance stats.
	 * @param change monthly maintenance cost difference
	 * @param wt - waytype for accounting purposes
	 */
	inline sint64 book_maintenance(sint64 change, waytype_t const wt)
	{
		transport_type tt = translate_waytype_to_tt(wt);
		maintenance[tt] += change;
		maintenance[TT_ALL] += change;
		return maintenance[tt];
	}

	/**
	 * Adds way renewal into/from finance stats (booked as a one off payment of infrastructure maintenance)
	 * @param renewal cost
	 * @param wt - waytype for accounting purposes
	 */
	inline void book_way_renewal(const sint64 amount, const waytype_t wt)
	{
		transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_INFRASTRUCTURE_MAINTENANCE] += amount;
		veh_month[tt][0][ATV_INFRASTRUCTURE_MAINTENANCE] += amount;

		account_balance += amount;
	}

	/**
	 * Adds way distance for player ranking stats
	 * @param straight=10, diagonal=7
	 * @param wt - waytype for accounting purposes
	 */
	inline void book_way_length(const sint64 amount, const waytype_t wt)
	{
		veh_year[TT_ALL][0][ATV_WAY_LENGTH] += amount;
		veh_month[TT_ALL][0][ATV_WAY_LENGTH] += amount;
		transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_WAY_LENGTH] += amount;
		veh_month[tt][0][ATV_WAY_LENGTH] += amount;
	}

	/**
	 * Account purchase of new vehicle: Subtracts money, increases assets.
	 * @param amount money paid for vehicle (negative)
	 * @param wt - waytype of vehicle
	 */
	inline void book_new_vehicle(const sint64 amount, const waytype_t wt)
	{
		// Note that for a new vehicle, amount is NEGATIVE
		// It is positive for a SALE of a vehicle
		const transport_type tt = translate_waytype_to_tt(wt);

		veh_year[ tt][0][ATV_NEW_VEHICLE] += amount;
		veh_month[tt][0][ATV_NEW_VEHICLE] += amount;

		update_assets(-amount, wt);

		account_balance += amount;
	}

	/**
	 * Adds count to number of vehicles in statistics.
	 */
	inline void book_vehicle_number(const int count, const waytype_t wt) {
		veh_year[TT_ALL][0][ATV_VEHICLES] += count;
		veh_month[TT_ALL][0][ATV_VEHICLES] += count;
		transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_VEHICLES] += count;
		veh_month[tt][0][ATV_VEHICLES] += count;
	}

	/**
	 * Accounts income from transport of passenger, mail, or, goods.
	 * @param amount earned money
	 * @param wt waytype of vehicle
	 * @param index 0 = passenger, 1 = mail, 2 = goods
	 */
	inline void book_revenue(const sint64 amount, const waytype_t wt, sint32 index)
	{
		const transport_type tt = translate_waytype_to_tt(wt);

		index = ((0 <= index) && (index <= 2)? index : 2);

		veh_year[tt][0][ATV_REVENUE_PASSENGER+index] += amount;
		veh_month[tt][0][ATV_REVENUE_PASSENGER+index] += amount;

		account_balance += amount;
	}

	/**
	 * Accounts distance-based running costs
	 * @param amount sum of money
	 * @param wt way type
	 */
	inline void book_running_costs(const sint64 amount, const waytype_t wt)
	{
		const transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_RUNNING_COST] += amount;
		veh_month[tt][0][ATV_RUNNING_COST] += amount;
		account_balance += amount;
	}

	/**
	 * Account toll we have paid to any other company.
	 * @param amount sum of money
	 * @param wt way type
	 */
	inline void book_toll_paid(const sint64 amount, const waytype_t wt)
	{
		const transport_type tt =  translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_TOLL_PAID] += amount;
		veh_month[tt][0][ATV_TOLL_PAID] += amount;
		account_balance += amount;
	}

	/**
	 * Account toll we have received from another company.
	 * @param amount sum of money
	 * @param wt way type
	 */
	inline void book_toll_received(const sint64 amount, const waytype_t wt)
	{
		const transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_TOLL_RECEIVED] += amount;
		veh_month[tt][0][ATV_TOLL_RECEIVED] += amount;
		account_balance += amount;
	}

	/**
	 * Makes stats of amount of transported passenger, mail and goods
	 * @param amount sum of money
	 * @param wt way type
	 * @param index 0 = passenger, 1 = mail, 2 = goods
	 */
	inline void book_transported(const sint64 amount, const waytype_t wt, int index)
	{
		const transport_type tt = translate_waytype_to_tt(wt);

		// there are: passenger, mail, goods
		if( (index < 0) || (index > 2)){
			index = 2;
		}

		veh_year[ tt][0][ATV_TRANSPORTED_PASSENGER+index] += amount;
		veh_month[tt][0][ATV_TRANSPORTED_PASSENGER+index] += amount;
	}

	/**
	 * Traveled distance of convoy/vehicle to player statistics.
	 * @param travel distance of convoy
	 * @param wt type of transport used for accounting statistics
	 * @param vehicle number of convoy
	 */
	inline void book_convoy_distance(const sint64 distance, const waytype_t wt, uint8 vehicle_count)
	{
		veh_year[TT_ALL][0][ATV_CONVOY_DISTANCE]   += distance;
		veh_month[TT_ALL][0][ATV_CONVOY_DISTANCE]  += distance;
		veh_year[TT_ALL][0][ATV_VEHICLE_DISTANCE]  += distance * vehicle_count;
		veh_month[TT_ALL][0][ATV_VEHICLE_DISTANCE] += distance * vehicle_count;
		const transport_type tt = translate_waytype_to_tt(wt);
		veh_year[tt][0][ATV_CONVOY_DISTANCE]   += distance;
		veh_month[tt][0][ATV_CONVOY_DISTANCE]  += distance;
		veh_year[tt][0][ATV_VEHICLE_DISTANCE]  += distance * vehicle_count;
		veh_month[tt][0][ATV_VEHICLE_DISTANCE] += distance * vehicle_count;
	}

	/**
	 * Accounts monthly vehicle maintenance costs
	 * Called monthly by *each vehicle* (therefore inline)
	 * "Monthly" amounts are adjusted for bits_per_month
	 * (so they're actually amounts per "x minutes")
	 * @param wt way type
	 * @author neroden
	 */
	inline void book_vehicle_maintenance_with_bits(const sint64 amount, const waytype_t wt) {
		const transport_type tt = translate_waytype_to_tt(wt);
		sint64 real_amount = world->calc_adjusted_monthly_figure(amount);
		veh_year[tt][0][ATV_VEHICLE_MAINTENANCE] += real_amount;
		veh_month[tt][0][ATV_VEHICLE_MAINTENANCE] += real_amount;
		account_balance += real_amount;
	}

	/**
	 * Calculates the finance history for player.
	 * This method has to be called before reading any variables besides account_balance!
	 */
	void calc_finance_history();

	/**
	 * @returns amount of money on account (also known as konto)
	 */
	inline sint64 get_account_balance() { return account_balance; }

	/**
	 * Is player allowed to purchase something of this price, or is player
	 * too deep in debt?  (Note that this applies to all players; the public
     * player is special-cased in player_t, and skips this routine.)
	 * @returns whether player is allowed to purchase something of cost "price"
	 * @params price
	 */
	inline bool can_afford(sint64 price) const
	{
		if (price <= 0) return true;  // Always allow things which generate money
		else if (account_balance - price >= get_soft_credit_limit() ) return true;
		else if ( world->get_settings().insolvent_purchases_allowed() ) return true;
		else if ( world->get_settings().is_freeplay() ) return true;
		else return false;
	}

	/**
	 * This is a negative number.  Upon having an account balance more negative than this,
	 * the player cannot purchase new things.
	 */
	inline sint64 get_soft_credit_limit() const { return com_month[0][ATC_SOFT_CREDIT_LIMIT]; }

	/**
	 * This is a negative number.  Upon having an account balance more negative than this,
	 * the player becomes insolvent.
	 */
	inline sint64 get_hard_credit_limit() const { return com_month[0][ATC_HARD_CREDIT_LIMIT]; }

	/**
	* Return the number of consecutiuve months for which this player has been insolvent, starting
	* with the current month. If the player is not currently insolvent, 0 is returned. The maximum
	* number returned is MAX_PLAYER_HISTORY_MONTHS.
	*/
	inline uint32 get_number_of_months_insolvent() const
	{
		if (account_balance >= com_month[0][ATC_HARD_CREDIT_LIMIT])
		{
			return 0;
		}

		uint32 months = 0;
		for (uint32 i = 0; i < MAX_PLAYER_HISTORY_MONTHS; i++)
		{
			if (com_month[i][ATC_CASH] < com_month[i][ATC_HARD_CREDIT_LIMIT])
			{
				months++;
			}
			else
			{
				break;
			}
		}

		return months;
	}

	/**
	 * Recalculate credit limits.
	 * Should not be called more than once a month.
	 */
	void calc_credit_limits();

	/**
	 * Book interest (once a month, please!)
	 */
	void book_interest_monthly();

	/**
	 * Books amount of money to account (also known as konto)
	 */
	void book_account(sint64 amount)
	{
		account_balance += amount;
		com_month[0][ATC_CASH] = account_balance;
		com_year [0][ATC_CASH] = account_balance;
		com_month[0][ATC_NETWEALTH] += amount;
		com_year [0][ATC_NETWEALTH] += amount;
		// BUG profit is not adjusted when calling this method
	}

	/**
	 * Returns the finance history (indistinguishable part) for player.
	 * Call calc_finance_history before use!
	 * @param year 0 .. current year, 1 .. last year, etc
	 * @param type one of accounting_type_common
	 */
	sint64 get_history_com_year(int year, int type) const { return com_year[year][type]; }
	sint64 get_history_com_month(int month, int type) const { return com_month[month][type]; }

	/**
	 * Returns the finance history (distinguishable by type of transport) for player.
	 * Call calc_finance_history before use!
	 * @param tt one of transport_type
	 * @param year 0 .. current year, 1 .. last year, etc
	 * @param type one of accounting_type_vehicles
	 */
	sint64 get_history_veh_year(transport_type tt, int year, int type) const { return veh_year[tt][year][type]; }
	sint64 get_history_veh_month(transport_type tt, int month, int type) const { return veh_month[tt][month][type]; }

	/**
	 * @return how much month we have been in red numbers (= we had negative account balance)
	 */
	sint32 get_account_overdrawn() const { return account_overdrawn; }

	/**
	 * @returns maintenance
	 * @param tt transport type (Truck, Ship Aircraft, ...)
	 */
	sint64 get_maintenance(transport_type tt=TT_ALL) const { assert(tt<TT_MAX); return maintenance[tt]; }

	/**
	 * @returns maintenance scaled with bits_per_month
	 */
	sint64 get_maintenance_with_bits(transport_type tt=TT_ALL) const;

	sint64 get_netwealth() const
	{
		// return com_year[0][ATC_NETWEALTH]; wont work as ATC_NETWEALTH is *only* updated in calc_finance_history
		// see calc_finance_history
		return veh_month[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] + account_balance;
	}

	sint64 get_financial_assets() const {return veh_month[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS];}

	sint64 get_scenario_completed() const { return com_month[0][ATC_SCENARIO_COMPLETED]; }

	void set_scenario_completed(sint64 percent) { com_year[0][ATC_SCENARIO_COMPLETED] = com_month[0][ATC_SCENARIO_COMPLETED] = percent; }

	sint64 get_starting_money() const { return starting_money; }

	/**
	 * @returns vehicle maintenance scaled with bits_per_month
	 */
	// sint64 get_vehicle_maintenance_with_bits(transport_type tt=TT_ALL) const;

	/**
	 * @returns TRUE if there is at least one convoi, otherwise returns false
	 */
	bool has_convoi() const { return (veh_year[TT_ALL][0][ATV_CONVOIS] > 0); }

	/**
	 * returns TRUE if net wealth > 0 (but this of course requires that we keep netwealth up to date!)
	 */
	bool has_money_or_assets() const { return ( get_netwealth() > 0 ); }

	/**
	 * increases number of month for which the company is in red numbers
	 */
	void increase_account_overdrawn() { account_overdrawn++; }

	/**
	 * Called at beginning of new month.
	 */
	void new_month();

	/**
	 * rolls the finance history for player (needed when new_year() or new_month()) triggered
	 */
	void roll_history_year();
	void roll_history_month();

	/**
	 * loads or saves finance statistic
	 */
	void rdwr(loadsave_t *file);

	/// loads statistics of old versions
	void rdwr_compatibility(loadsave_t *file);

	/**
	 * Sets account balance. This method enables to load old game format.
	 * Do NOT use it in any other places!
	 */
	void set_account_balance(sint64 amount) { account_balance = amount; }

	void set_assets(const sint64 (&assets)[TT_MAX]);

	/**
	 * Sets number of months for that the account balance is below zero. This method enables to load old game format.
	 * Do NOT use it in any other places for any other purpose!
	 */
	void set_account_overdrawn(sint32 num) { account_overdrawn = num; }

	void set_starting_money(sint64 amount) {  starting_money = amount; }

	/**
	 * Translates building_desc_t to transport_type
	 * Building can be assigned to transport type using utyp
	 */
	static transport_type translate_utyp_to_tt(int utyp);

	/**
	 * Translates waytype_t to transport_type
	 */
	static transport_type translate_waytype_to_tt(waytype_t wt);

	// to tranlate back to strings for finances GUI
	static const char* transport_type_values[TT_MAX];

	static waytype_t translate_tt_to_waytype(transport_type tt);

	void update_assets(sint64 delta, waytype_t wt);

private:
	/**
	 * Subroutine for credit limits
	 * @author neroden
	 */
	sint64 credit_limit_by_assets() const;

	/**
	 * Subroutine for credit limits
	 * @author neroden
	 */
	sint64 credit_limit_by_profits() const;

	/**
	 * Translates finance statistics from new format to old one.
	 * Used for saving data in old format
	 */
	void export_to_cost_month(sint64 finance_history_month[][OLD_MAX_PLAYER_COST]);
	void export_to_cost_year( sint64 finance_history_year[][OLD_MAX_PLAYER_COST]);


	/**
	 * Translates finance statistics from old format to new one.
	 * Used for loading data from old format
	 */
	void import_from_cost_month(const sint64 finance_history_month[][OLD_MAX_PLAYER_COST]);
	void import_from_cost_year( const sint64 finance_history_year[][OLD_MAX_PLAYER_COST]);
};

#endif
