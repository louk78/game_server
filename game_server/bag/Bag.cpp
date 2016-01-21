/*
 * Bag.cpp
 *
 *  Created on: 2016年1月19日
 *      Author: zhangyalei
 */

#include "Bag.h"
#include "Bag_Func.h"
#include "Game_Manager.h"
#include "Game_Player.h"
#include "Configurator.h"
#include "Log.h"
#include "DB_Manager.h"

Bag::Bag(void) : player_(0), seq_generator_(0) { }

Bag::~Bag(void) { }

void Bag::reset(void) {
	seq_generator_ = 0;
	bag_info_.reset();
}

int Bag::load_detail(Player_Data &data) {
	bag_info_ = data.bag_info;
	for (Bag_Info::Item_Map::iterator it = bag_info_.item_map.begin();
			it != bag_info_.item_map.end(); ++it) {
		it->second->item_basic.seq = ++seq_generator_;
	}

	return 0;
}

int Bag::save_detail(Player_Data &data) {
	data.bag_info = bag_info_;
	bag_info_.is_change_ = false;
	return 0;
}

int Bag::init(Game_Player *player) {
	player_ = player;
	if (bag_info_.capacity.pack_cap < Bag_Info::PACK_INIT_CAPACITY) {
		bag_info_.capacity.pack_cap = Bag_Info::PACK_INIT_CAPACITY;
	}

	if (bag_info_.capacity.storage_cap < Bag_Info::STORAGE_INIT_CAPACITY) {
		bag_info_.capacity.storage_cap = Bag_Info::STORAGE_INIT_CAPACITY;
	}
	return 0;
}

int Bag::create_init(Player_Data &data) {
	return 0;
}

int Bag::pack_insert_equip(const uint32_t index, const Item_Info &item) {
	if (is_item_exist(index)) {
		return ERROR_SERVER_INNER;
	}
	uint32_t begin, end;
	pack_get_index(PACKER_T_EQUIP_INDEX, begin, end);
	if (index < begin || index > end) {
		return ERROR_SERVER_INNER;
	}

	if (item.type != Item_Info::EQUIP) {
		return ERROR_SERVER_INNER;
	}

	int stack_upper = Item_Info::get_item_stack_upper(item.item_basic.id);
	int amount = item.item_basic.amount;
	Item_Info *item_temp = GAME_MANAGER->pop_item();
	*item_temp = item;
	item_temp->item_basic.index = index;
	item_temp->item_basic.amount = std::min(amount, stack_upper);
	item_temp->item_basic.seq = ++seq_generator_;
	bag_info_.item_map.insert(std::make_pair(index, item_temp));

	UInt_Set change_set;
	change_set.insert(index);
	pack_active_update_item_detail(&change_set);
	return 0;
}

int Bag::pack_insert_item(const Packer_Type pack_type, const Item_Info &item, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	// 若物品可堆叠，优先叠放
	if (Item_Info::get_item_stack_upper(item.item_basic.id) > 1) {
		result = pack_insert_to_exist_index_first(pack_type, item, &changed_set, GENERATE_SEQ, pack_try);
	} else {
		result = pack_insert_to_empty_index_direct(pack_type, item, &changed_set, GENERATE_SEQ, pack_try);
	}

	if (result == 0) {
		pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_insert_item(const Packer_Type pack_type, const std::vector<Item_Info> &item_list, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_insert_to_exist_index_first(pack_type, item_list, &changed_set, GENERATE_SEQ, pack_try);

	if (result == 0) {
		pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_erase_item(const Packer_Type pack_type, const Id_Amount &id_amount, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(pack_type, id_amount, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_erase_item(const Packer_Type pack_type, const std::vector<Id_Amount> &id_amount_list, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(pack_type, id_amount_list, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_erase_item(const Index_Amount &index_amount, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(index_amount, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_fetch_bag_info(const MSG_120100 &msg) {
	Packer_Type pack_type = static_cast<Packer_Type>(msg.pack_type);

	switch (pack_type) {
	case PACKER_T_PACKAGE_INDEX:
	case PACKER_T_EQUIP_INDEX:
		break;
	default:
		return player_->respond_error_result(RES_FETCH_BAG_INFO, ERROR_CLIENT_PARAM);
	}

	MSG_520100 msg_back;
	msg_back.pack_type = msg.pack_type;
	msg_back.capacity = pack_get_capacity(pack_type);

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	for (Bag_Info::Item_Map::iterator item_it = it_begin; item_it != it_end; ++item_it) {
		pack_insert_item_to_msg(*item_it->second, msg_back);
	}

	Block_Buffer buf;
	msg_back.serialize(buf);
	return player_->respond_success_result(RES_FETCH_BAG_INFO, &buf);
}

int Bag::pack_add_capacity(const MSG_120101 &msg) {
	if (msg.add_capacity <= 0) {
		return player_->respond_success_result(RES_ADD_CAPACITY, NULL);
	}
	int add = 0;
	int price = 0;
	uint32_t caps = 0;
	int result = 0;
	Packer_Type pack_type = static_cast<Packer_Type>(msg.pack_type);
	switch (msg.pack_type) {
	case PACKER_T_PACKAGE_INDEX:
		caps = bag_info_.capacity.pack_cap;
		if (msg.pay_type != 1) {
			caps = std::min(static_cast<uint32_t>(Bag_Info::PACK_MAX_CAPACITY), caps + msg.add_capacity);
			add = caps - bag_info_.capacity.pack_cap;
		} else {
			caps = caps + msg.add_capacity;
			int caps_lose = caps % PACKAGE_GRID_PER_LINE;
			if (caps_lose != 0) {
				return player_->respond_error_result(RES_ADD_CAPACITY, ERROR_CLIENT_OPERATE);
			}
			caps = std::min(static_cast<uint32_t>(Bag_Info::PACK_MAX_CAPACITY), caps);
			add = caps - bag_info_.capacity.pack_cap;
		}
		if (add > 0) {
			if (msg.pay_type != 1) {
				result = get_capacity_price(pack_type, msg.pay_type, bag_info_.capacity.pack_cap, bag_info_.capacity.pack_cap + add, price);
			}
			if (result == 0) {
				switch (msg.pay_type) {
				case 0: {
					int item_id = 0; //CONFIG_INSTANCE->package_config()["item_id"].asInt();
					result = pack_erase_item(PACKER_T_PACKAGE_INDEX, Id_Amount(item_id, price));
					break;
				}
				case 1: {
					int item_id = 0; //CONFIG_INSTANCE->package_config()["super_item_id"].asInt();
					int item_nums = 0;
					get_capacity_super_item_nums(bag_info_.capacity.pack_cap, caps, PACKER_T_PACKAGE_INDEX, item_nums);
					result = pack_erase_item(PACKER_T_PACKAGE_INDEX, Id_Amount(item_id, item_nums));
					break;
				}
				case 2:
					result = pack_sub_money(Money_Sub_Info(COUPON_ONLY, price));
					break;
				case 3:
					result = pack_sub_money(Money_Sub_Info(GOLD_ONLY, price));
					break;
				default:
					result = ERROR_CLIENT_OPERATE;
					break;
				}
			}
			if (result == 0) {
				bag_info_.capacity.pack_cap = caps;
			}
		}
		break;
	case PACKER_T_STORAGE_INDEX:
		caps = bag_info_.capacity.storage_cap;
		if (msg.pay_type != 1) {
			caps = std::min(static_cast<uint32_t>(Bag_Info::STORAGE_MAX_CAPACITY), caps + msg.add_capacity);
			add = caps - bag_info_.capacity.storage_cap;
		} else {
			caps = caps + msg.add_capacity;
			int caps_lose = caps % STORAGE_GRID_PER_LINE;
			if (caps_lose != 0) {
				return player_->respond_error_result(RES_ADD_CAPACITY, ERROR_CLIENT_OPERATE);
			}
			caps = std::min(static_cast<uint32_t>(Bag_Info::STORAGE_MAX_CAPACITY), caps);
			add = caps - bag_info_.capacity.storage_cap;
		}

		if (add > 0) {
			if (msg.pay_type != 1) {
				result = get_capacity_price(pack_type, msg.pay_type, bag_info_.capacity.storage_cap, bag_info_.capacity.storage_cap + add, price);
			}
			if (result == 0) {
				switch (msg.pay_type) {
				case 0: {
					int item_id = 0; //CONFIG_INSTANCE->package_config()["item_id"].asInt();
					result = pack_erase_item(PACKER_T_PACKAGE_INDEX, Id_Amount(item_id, price));
					break;
				}
				case 1: {
					int item_id = 0; //CONFIG_INSTANCE->package_config()["super_item_id"].asInt();
					int item_nums = 0;
					get_capacity_super_item_nums(bag_info_.capacity.storage_cap, caps, PACKER_T_STORAGE_INDEX, item_nums);
					result = pack_erase_item(PACKER_T_PACKAGE_INDEX, Id_Amount(item_id, item_nums));
					break;
				}
				case 2:
					result = pack_sub_money(Money_Sub_Info(COUPON_ONLY, price));
					break;
				case 3:
					result = pack_sub_money(Money_Sub_Info(GOLD_ONLY, price));
					break;
				default:
					result = ERROR_CLIENT_OPERATE;
					break;
				}
			}
			if (result == 0) {
				bag_info_.capacity.storage_cap = caps;
			}
		}

		break;
	default:
		result = ERROR_CLIENT_PARAM;
		break;
	}
	bag_info_.is_change_ = true;

	this->pack_active_update_capacity(pack_type);

	return player_->respond_error_result(RES_ADD_CAPACITY, result);
}

int Bag::pack_discard_item(const MSG_120102 &msg) {
	int result = pack_try_erase_item(msg.index);
	if (result != 0) {
		return player_->respond_error_result(RES_DISCARD_ITEM, result);
	}

	pack_erase_item(msg.index, WITHOUT_TRY);
	player_->respond_success_result(RES_DISCARD_ITEM, NULL);
	return 0;
}

int Bag::pack_move_item(const MSG_120103 &msg) {
	int result = 0;
	switch (msg.index_to) {
	case PACKER_T_PACKAGE_INDEX:
	case PACKER_T_STORAGE_INDEX:
		result = this->pack_move_item(msg.index_from, static_cast<Packer_Type>(msg.index_to));
		break;
	default:
		if (pack_is_pack_type(msg.index_to, PACKER_T_EQUIP_INDEX) == 0) {
			result = ERROR_CLIENT_PARAM;
		}
		if (result == 0) {
			result = this->pack_move_item(msg.index_from, msg.index_to);
		}
		break;
	}

	player_->respond_error_result(RES_MOVE_ITEM, result);
	return 0;
}

int Bag::pack_split_item(const MSG_120104 &msg) {
	if (pack_is_pack_type(msg.index, PACKER_T_PACKAGE_INDEX) != 0) {
		return player_->respond_error_result(RES_SPLIT_ITEM, ERROR_CLIENT_OPERATE);
	}

	if (is_item_lock(msg.index)) {
		return player_->respond_error_result(RES_SPLIT_ITEM, ERROR_PACK_LOCK);
	}

	Bag_Info::Item_Map::iterator it = bag_info_.item_map.find(msg.index);
	if (it == bag_info_.item_map.end()) {
		return player_->respond_error_result(RES_SPLIT_ITEM, ERROR_CLIENT_OPERATE);
	} else if (Item_Info::get_item_stack_upper(it->second->item_basic.id) <= 1
			|| msg.split_num - it->second->item_basic.amount >= 0) {
		return player_->respond_error_result(RES_SPLIT_ITEM, ERROR_CLIENT_OPERATE);
	}

	UInt_Set changed_set;
	Item_Info item(*it->second);
	item.item_basic.amount = msg.split_num;

	int result = pack_insert_to_empty_index_direct(PACKER_T_PACKAGE_INDEX, item, &changed_set, GENERATE_SEQ);
	if (result == 0) {
		it->second->item_basic.amount -= msg.split_num;
		changed_set.insert(msg.index);
		pack_active_update_item_detail(&changed_set);
	}

	return player_->respond_error_result(RES_SPLIT_ITEM, result);
}

int Bag::pack_sort_item(const MSG_120105 &msg) {
	int result = this->pack_sort_item(static_cast<Packer_Type>(msg.pack_type), MERGE_WAY_EQUAL);
	return player_->respond_error_result(RES_SORT_ITEM, result);
}

int Bag::pack_merge_item(const MSG_120108 &msg) {
	int result = 0;
	if (msg.pack_type != PACKER_T_PACKAGE_INDEX && msg.pack_type != PACKER_T_STORAGE_INDEX) {
		result = ERROR_CLIENT_PARAM;
	} else {
		result = this->pack_sort_item(static_cast<Packer_Type>(msg.pack_type), MERGE_WAY_SIMILAR);
	}

	player_->respond_error_result(RES_MERGE_ITEM, result);
	return 0;
}

int Bag::pack_sell_item(const MSG_120109 &msg) {
	// 若能删除，则物品存在，下面无需判断
	int result = pack_try_erase_item(msg.index);
	if (result != 0) {
		return player_->respond_error_result(RES_SELL_ITEM, result);
	}

	std::vector<Id_Amount> id_amount_list;
	std::vector<const Item_Info*> item_vec;
	int item_nums = msg.index.size();
	id_amount_list.resize(item_nums);
	item_vec.resize(item_nums);
	for (int i = 0; i < item_nums; ++i) {
		item_vec[i] = pack_get_const_item(msg.index[i]);
		id_amount_list[i].id = item_vec[i]->item_basic.id;
		id_amount_list[i].amount = item_vec[i]->item_basic.amount;
	}

	Money_Add_List add_info;
	result = pack_try_add_money(add_info);
	if (result != 0) {
		return player_->respond_error_result(RES_SELL_ITEM, result);
	}

	// 删除指定位置物品
	pack_erase_item(msg.index, WITHOUT_TRY);
	pack_add_money(add_info);

	return player_->respond_success_result(RES_SELL_ITEM, NULL);
}

struct Item_Detail_PtrLess {
	bool operator() (const Item_Info *item1, const Item_Info *item2) const {
		return *item1 < *item2;
	}
};
int Bag::pack_sort_item(const Packer_Type pack_type, MERGE_WAY merge_way) {
	if (is_item_lock()) {
		return ERROR_PACK_LOCK;
	}

	std::vector<Item_Info*> item_array;

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);
	item_array.reserve(std::distance(it_begin, it_end));

	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		item_array.push_back(it->second);
	}

	std::stable_sort(item_array.begin(), item_array.end(), Item_Detail_PtrLess());
	UInt_Set changed_set;
	if (merge_way == MERGE_WAY_SIMILAR) {
		pack_merge_item_array(item_array, pack_type, MERGE_WAY_SIMILAR, changed_set);
	} else {
		pack_merge_item_array(item_array, pack_type, MERGE_WAY_EQUAL, changed_set);
	}

	// 不将指针归还对象池，仅归还合并掉的
	bag_info_.item_map.erase(it_begin, it_end);
	const uint32_t array_size = item_array.size();
	Bag_Info::Item_Map::iterator it_pos = bag_info_.item_map.upper_bound(pack_type);
	for (uint32_t i = 0; i < array_size; ++i) {
		if (item_array[i]->item_basic.amount > 0) {
			it_pos = bag_info_.item_map.insert(it_pos, std::make_pair(item_array[i]->item_basic.index, item_array[i]));
		} else {
			GAME_MANAGER->push_item(item_array[i]);
		}
	}

	// update all changed by sort to client
	return pack_active_update_item_detail(&changed_set);
}

int Bag::pack_set_money(const Money_Info &money_info, Money_Opt_Type type) {
	if (money_info.bind_copper < 0 || money_info.copper < 0 ||
			money_info.coupon < 0 || money_info.gold < 0) {
		return -1;
	}
	bag_info_.money_info = money_info;
	pack_active_update_money();

	return 0;
}

int Bag::pack_try_erase_item(const Packer_Type pack_type, const std::vector<Id_Amount> &id_amount_list) {
	// 合并可重复id
	std::map<Id_Amount, int32_t> amount_map;
	for (std::vector<Id_Amount>::const_iterator it = id_amount_list.begin(); it != id_amount_list.end(); ++it) {
		if (it->amount > 0) {
			amount_map[*it] += it->amount;
		} else {
			return ERROR_CLIENT_OPERATE;
		}
	}

	for (std::map<Id_Amount, int32_t>::iterator map_it = amount_map.begin(); map_it != amount_map.end(); ++map_it) {
		if (map_it->second <= 0) {
			return ERROR_CLIENT_OPERATE;
		}
		if (pack_calc_item(pack_type, map_it->first.id, map_it->first.bind_verify) < map_it->second)
			return ERROR_ITEM_NOT_ENOUGH;
	}

	if (is_item_lock()) { // 存在物品被锁定的情况下，要检测是否有物品被锁定导致不能扣除
		for (std::map<Id_Amount, int32_t>::iterator map_it = amount_map.begin(); map_it != amount_map.end(); ++map_it) {
			if (pack_calc_unlock_item(pack_type, map_it->first.id, map_it->first.bind_verify) < map_it->second) return ERROR_PACK_LOCK;
		}
	}

	return 0;
}

int Bag::pack_erase_item(const Packer_Type pack_type, const Id_Amount &id_amount, UInt_Set *changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		if (id_amount.amount <= 0) return ERROR_CLIENT_PARAM;
		int result = pack_try_erase_item(pack_type, id_amount);
		if (result != 0) return result;
	}

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);
	std::vector<uint32_t> erase_index;
	int32_t amount_temp = id_amount.amount;
	int32_t dec_per_once = 0;
	Bind_Verify bind_verify = id_amount.bind_verify;
	if (id_amount.bind_verify == BIND_NONE) {
		bind_verify = BIND_ONLY;
	}
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end && amount_temp > 0; ++it) {
		if (is_item_lock(it->first)) {
			continue;
		}
		if (pack_is_req_item(*it->second, id_amount.id, bind_verify)) {
			dec_per_once = std::min(it->second->item_basic.amount, amount_temp);
			it->second->item_basic.amount -= dec_per_once;

			amount_temp -= dec_per_once;
			if (NULL != changed_set) {
				changed_set->insert(it->first);
			}
			if (it->second->item_basic.amount == 0) {
				erase_index.push_back(it->first);
			}
		}
	}

	if (id_amount.bind_verify == BIND_NONE) {
		for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end && amount_temp > 0; ++it) {
			if (is_item_lock(it->first)) {
				continue;
			}
			if (pack_is_req_item(*it->second, id_amount.id, UNBIND_ONLY)) {
				dec_per_once = std::min(it->second->item_basic.amount, amount_temp);
				it->second->item_basic.amount -= dec_per_once;

				amount_temp -= dec_per_once;
				if (NULL != changed_set) {
					changed_set->insert(it->first);
				}
				if (it->second->item_basic.amount == 0) {
					erase_index.push_back(it->first);
				}
			}
		}
	}

	for (std::vector<uint32_t>::iterator it = erase_index.begin(); it != erase_index.end(); ++it) {
		bag_info_.erase(*it);
	}

	return 0;
}

int Bag::pack_erase_item(const Packer_Type pack_type, const std::vector<Id_Amount> &id_amount_list, UInt_Set *changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_erase_item(pack_type, id_amount_list);
		if (result != 0) return result;
	}

	for (std::vector<Id_Amount>::const_iterator it = id_amount_list.begin(); it != id_amount_list.end(); ++it) {
		pack_erase_item(pack_type, *it, changed_set, WITHOUT_TRY);
	}

	return 0;
}

int Bag::pack_erase_item(const Index_Amount &index_amount, UInt_Set *changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_erase_item(index_amount);
		if (result != 0) return result;
	}

	Bag_Info::Item_Map::iterator it = bag_info_.item_map.find(index_amount.index);
	if (it->second->item_basic.amount == index_amount.amount) {
		bag_info_.erase(it);
	} else {
		it->second->item_basic.amount -= index_amount.amount;
	}

	if (NULL != changed_set) {
		changed_set->insert(index_amount.index);
	}

	return 0;
}

int Bag::pack_try_insert_item(const Packer_Type pack_type, const std::vector<Item_Info> &item_list) {
	// 装备不允许用insert的接口插入，必须从装备模块处操作
	if (pack_type == PACKER_T_EQUIP_INDEX) return ERROR_SERVER_INNER;
	// item.json中不存在的item，id错误
	for (std::vector<Item_Info>::const_iterator it = item_list.begin(); it != item_list.end(); ++it) {
		//if (CONFIG_INSTANCE->item(it->item_basic.id) == Json::Value::null) {
		//	MSG_USER("item %d config not exist", it->item_basic.id);
		//	return ERROR_ITEM_NOT_EXIST;
		//}
	}
	// 合并可重复item
	std::map<Item_Info, int32_t> amount_map;
	for (std::vector<Item_Info>::const_iterator it = item_list.begin(); it != item_list.end(); ++it) {
		if (it->item_basic.amount <= 0) {
			return ERROR_CLIENT_OPERATE;
		}
		amount_map[*it] += it->item_basic.amount;
	}

	const int remain_cap = pack_get_remain_capacity(pack_type);
	int capacity_add = 0;
	for (std::map<Item_Info, int32_t>::iterator map_it = amount_map.begin(); map_it != amount_map.end(); ++map_it) {
		int stack_upper = Item_Info::get_item_stack_upper(map_it->first.item_basic.id);
		int stackable = 0;
		int amount = map_it->second;
		Bag_Info::Item_Map::iterator it_begin;
		Bag_Info::Item_Map::iterator it_end;
		pack_get_iter(pack_type, it_begin, it_end);
		for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
			if (is_equal_item(map_it->first, *it->second)) {
				stackable += stack_upper - it->second->item_basic.amount;
			}
		}

		if (amount > stackable) {
			int amount_more = amount - stackable;
			capacity_add += (amount_more / stack_upper);
			if (amount_more % stack_upper != 0) {
				capacity_add++;
			}
			if (remain_cap < capacity_add) {
				return get_full_error_code(pack_type);
			}
		}
	}

	return 0;
}

int Bag::pack_insert_to_exist_index_first(const Packer_Type pack_type, const std::vector<Item_Info> &item_list, UInt_Set *changed_set,
		Seq_Type seq_type, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_insert_item(pack_type, item_list);
		if (result != 0) return result;
	}

	for (std::vector<Item_Info>::const_iterator it = item_list.begin(); it != item_list.end(); ++it) {
		if (pack_insert_to_exist_index_first(pack_type, *it, changed_set, seq_type, WITHOUT_TRY) != 0) {
			MSG_USER("pack insert error.");
		}
	}

	return 0;
}

int Bag::pack_insert_to_exist_index_first(const Packer_Type pack_type, const Item_Info &item,
		UInt_Set *changed_set, Seq_Type seq_type, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_insert_item(pack_type, item);
		if (result != 0) return result;
	}

	int amount = item.item_basic.amount;
	int stack_upper = Item_Info::get_item_stack_upper(item.item_basic.id);
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		if (amount == 0) {
			break;
		}
		if (is_equal_item(*it->second, item) && stack_upper - it->second->item_basic.amount > 0) {
			int remain_space = stack_upper - it->second->item_basic.amount;
			if (amount - (stack_upper - it->second->item_basic.amount) > 0) {
				amount -= remain_space;
				it->second->item_basic.amount += remain_space;
			} else {
				it->second->item_basic.amount += amount;
				amount = 0;
			}
			if (NULL != changed_set) {
				changed_set->insert(it->first);
			}
		}
	}

	if (amount != 0) {
		Item_Info item_temp(item);
		item_temp.item_basic.amount = amount;
		return pack_insert_to_empty_index_direct(pack_type, item_temp, changed_set, seq_type, WITHOUT_TRY);
	}

	return 0;
}

int Bag::pack_try_insert_to_empty_index_direct(const Packer_Type pack_type, const Item_Info &item) {
	if (item.item_basic.amount < 0) {
		return ERROR_CLIENT_PARAM;
	}

	int amount = item.item_basic.amount;
	int stack_upper = Item_Info::get_item_stack_upper(item.item_basic.id);
	int capacity_add = (amount / stack_upper);
	if (amount % stack_upper != 0) {
		capacity_add++;
	}

	if (pack_get_remain_capacity(pack_type) < capacity_add) {
		return get_full_error_code(pack_type);
	}

	return 0;
}

void Bag::get_equip_index(const Packer_Type pack_type, Int_Vec &equip_index) {
	equip_index.clear();

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		if (it->second->type == Item_Info::EQUIP) {
			equip_index.push_back(it->first);
		}
	}
}

void Bag::pack_get_indexes(const Packer_Type pack_type, const uint32_t id, Int_Vec &indexes) {
	indexes.clear();
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		if (it->second->item_basic.id == id) {
			indexes.push_back(it->first);
		}
	}
}

int Bag::pack_calc_item(const uint32_t index) {
	int amount = 0;
	Bag_Info::Item_Map::iterator it = bag_info_.item_map.find(index);
	if (it != bag_info_.item_map.end()) {
		amount = it->second->item_basic.amount;
	}

	if (amount < 0) MSG_USER("item amount < 0 in index %d.\n", index);
	return amount;
}

Item_Info *Bag::pack_get_item(const uint32_t index) {
	return pack_get_item_pointer(index);
}

const Item_Info *Bag::pack_get_const_item(const uint32_t index) {
	return pack_get_item_pointer(index);
}

int Bag::pack_is_pack_type(const int32_t index, Packer_Type pack_type) {
	bool is_infer_type = false;
	if (PACKER_T_EQUIP_INDEX < index && index <= PACKER_T_EQUIP_INDEX + bag_info_.capacity.equip_cap) {
		is_infer_type = (pack_type == PACKER_T_EQUIP_INDEX);
	} else if (PACKER_T_PACKAGE_INDEX < index && index <= PACKER_T_PACKAGE_INDEX + bag_info_.capacity.pack_cap) {
		is_infer_type = (pack_type == PACKER_T_PACKAGE_INDEX);
	} else if (PACKER_T_STORAGE_INDEX < index && index <= PACKER_T_STORAGE_INDEX + bag_info_.capacity.storage_cap) {
		is_infer_type = (pack_type == PACKER_T_STORAGE_INDEX);
	}
	return is_infer_type? 0 : -1;
}

int Bag::pack_get_pack_type(const int32_t index, Packer_Type &pack_type) {
	if (PACKER_T_EQUIP_INDEX < index && index <= PACKER_T_EQUIP_INDEX + bag_info_.capacity.equip_cap) {
		pack_type = PACKER_T_EQUIP_INDEX;
	} else if (PACKER_T_PACKAGE_INDEX < index && index <= PACKER_T_PACKAGE_INDEX + bag_info_.capacity.pack_cap) {
		pack_type = PACKER_T_PACKAGE_INDEX;
	} else if (PACKER_T_STORAGE_INDEX < index && index <= PACKER_T_STORAGE_INDEX + bag_info_.capacity.storage_cap) {
		pack_type = PACKER_T_STORAGE_INDEX;
	} else {
		return -1;
	}

	return 0;
}

int Bag::pack_get_item_id(const uint32_t index, uint32_t &id) {
	Bag_Info::Item_Map::iterator it = bag_info_.item_map.find(index);
	if (it != bag_info_.item_map.end()) {
		id = it->second->item_basic.id;
		return 0;
	}
	return ERROR_ITEM_NOT_EXIST;
}

int Bag::pack_move_item(const uint32_t index_from, const Packer_Type pack_type_to, UInt_Set &changed, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_move_item(index_from, pack_type_to);
		if (result != 0) return result;
	}

	Item_Info item = *(pack_get_const_item(index_from));
	changed.insert(index_from);
	bag_info_.erase(index_from);
	pack_insert_to_exist_index_first(pack_type_to, item, &changed, DONT_GENERATE_SEQ, WITHOUT_TRY);

	return 0;
}

int Bag::pack_move_item(const uint32_t index_from, const Packer_Type pack_type_to, Pack_Try pack_try ) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_move_item(index_from, pack_type_to);
		if (result != 0) return result;
	}

	Item_Info item = *(pack_get_const_item(index_from));
	UInt_Set changed_set;
	changed_set.insert(index_from);
	bag_info_.erase(index_from);
	pack_insert_to_exist_index_first(pack_type_to, item, &changed_set, DONT_GENERATE_SEQ, WITHOUT_TRY);

	return pack_active_update_item_detail(&changed_set);
}

int Bag::pack_move_item(const Packer_Type pack_type_from, const Packer_Type pack_type_to, Pack_Try pack_try ) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_move_item(pack_type_from, pack_type_to);
		if (result != 0) return result;
	}

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type_from, it_begin, it_end);

	UInt_Set changed_set;
	std::vector<Item_Info> item_list;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		item_list.push_back(*it->second);
		changed_set.insert(it->first);
	}

	bag_info_.erase(it_begin, it_end);
	pack_insert_to_exist_index_first(pack_type_to, item_list, &changed_set, DONT_GENERATE_SEQ, WITHOUT_TRY);

	return pack_active_update_item_detail(&changed_set);
}

int Bag::pack_attempt_move_everyitem(const Packer_Type pack_type_from, const Packer_Type pack_type_to, int &move_item_nums) {
	move_item_nums = 0;
	if (pack_type_from == pack_type_to) {
		return ERROR_CLIENT_OPERATE;
	}

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type_from, it_begin, it_end);
	if (it_begin == it_end) {
		return ERROR_NO_ITEM_GET;
	}

	std::vector<Index_Amount> item_vec;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		item_vec.push_back(Index_Amount(it->first, it->second->item_basic.amount));
	}

	// 尝试移动每个道具
	UInt_Set changed;
	for (std::vector<Index_Amount>::iterator it = item_vec.begin(); it != item_vec.end(); ++it) {
		int item_nums = it->amount;
		if (0 == pack_move_item(it->index, pack_type_to, changed)) {
			move_item_nums += item_nums;
		}
	}

	if (move_item_nums == 0) {
		return get_full_error_code(pack_type_to);
	} else {
		pack_active_update_item_detail(&changed);
		return 0;
	}
}

int Bag::pack_insert_to_empty_index_direct(const Packer_Type pack_type, const Item_Info &item,
		UInt_Set *changed_set, Seq_Type seq_type, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_insert_to_empty_index_direct(pack_type, item);
		if (result != 0) {
			return result;
		}
	}

	int amount = item.item_basic.amount;
	int stack_upper = Item_Info::get_item_stack_upper(item.item_basic.id);
	// 由于空的格子需要多个值来记录多个背包的空格，目前的实现较难加入此值，如有性能问题，再考虑加入空格子的标记
	uint32_t index_begin = 0;
	uint32_t index_end = 0;
	pack_get_index(pack_type, index_begin, index_end);
	for (uint32_t i = index_begin; amount > 0 && i < index_end; ++i) {
		if (!is_item_exist(i)) {
			Item_Info *item_temp = GAME_MANAGER->pop_item();
			*item_temp = item;
			item_temp->item_basic.index = i;
			item_temp->item_basic.amount = std::min(amount, stack_upper);
			amount -= item_temp->item_basic.amount;
			if (seq_type == GENERATE_SEQ) {
				item_temp->item_basic.seq = ++seq_generator_;
			}
			bag_info_.item_map.insert(std::make_pair(i, item_temp));
			if (NULL != changed_set) {
				changed_set->insert(i);
			}
		}
	}

	return 0;
}

int Bag::pack_calc_item(const Packer_Type pack_type) {
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	uint32_t item_amount = 0;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		item_amount += it->second->item_basic.amount;
	}

	return item_amount;
}

int Bag::pack_calc_item(const Packer_Type pack_type, const uint32_t id, Bind_Verify bind_verify) {
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	uint32_t item_amount = 0;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		if (pack_is_req_item(*it->second, id, bind_verify)) {
			item_amount += it->second->item_basic.amount;
		}
	}

	return item_amount;
}

int Bag::pack_calc_unlock_item(const Packer_Type pack_type, const uint32_t id, Bind_Verify bind_verify) {
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	uint32_t item_amount = 0;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		if (pack_is_req_item(*it->second, id, bind_verify)) {
			if (!is_item_lock(it->first)) {
				item_amount += it->second->item_basic.amount;
			}
		}
	}

	return item_amount;
}

int Bag::pack_active_update_capacity(const Packer_Type pack_type) {
	Block_Buffer buf;
	MSG_300102 msg;
	msg.type = pack_type;
	msg.capacity = pack_get_capacity(pack_type);

	msg.serialize(buf);

	return player_->respond_success_result(ACTIVE_BAG_CAPACITY, &buf);
}

int Bag::pack_active_update_money(void) {
	Block_Buffer buf;
	MSG_300103 msg;
	msg.money = bag_info_.money_info.money();
	msg.serialize(buf);
	player_->respond_success_result(ACTIVE_MONEY_INFO, &buf);

	bag_info_.save_tick();

	return 0;
}

int Bag::pack_active_update_item_detail(const UInt_Set *changed_index) {
	if (NULL == changed_index) return -1;

	// 不同背包要分别发送
	std::map<Packer_Type, MSG_300100> update_item_msg_map;
	std::map<Packer_Type, MSG_300101> update_addit_msg_map;
	std::map<Packer_Type, MSG_300104> erase_item_msg_map;

	for (UInt_Set::const_iterator index_it = changed_index->begin(); index_it != changed_index->end(); ++index_it) {
		Packer_Type type;
		pack_get_pack_type(*index_it, type);
		Bag_Info::Item_Map::iterator item_it = bag_info_.item_map.find(*index_it);
		if (item_it == bag_info_.item_map.end()) {
			erase_item_msg_map[type].item_erased.push_back(*index_it);
		} else {
			pack_insert_item_to_msg(*item_it->second, update_item_msg_map[type], update_addit_msg_map[type]);
		}
	}

	Block_Buffer buf;
	for (std::map<Packer_Type, MSG_300104>::iterator it = erase_item_msg_map.begin(); it != erase_item_msg_map.end(); ++it) {
		buf.reset();
		if (it->second.item_erased.size() > 0) {
			it->second.serialize(buf);
			player_->respond_success_result(ACTIVE_ITEM_ERASE, &buf);
		}
	}

	// 客户端要求附加信息移动到基础信息之前
	for (std::map<Packer_Type, MSG_300101>::iterator it = update_addit_msg_map.begin(); it != update_addit_msg_map.end(); ++it) {
		buf.reset();
		it->second.serialize(buf);
		player_->respond_success_result(ACTIVE_ITEM_ADDIT_INFO, &buf);
	}

	for (std::map<Packer_Type, MSG_300100>::iterator it = update_item_msg_map.begin(); it != update_item_msg_map.end(); ++it) {
		buf.reset();
		if (it->second.item_info.size() > 0) {
			it->second.serialize(buf);
			player_->respond_success_result(ACTIVE_ITEM_INFO, &buf);
		}
	}

	bag_info_.save_tick();

	return 0;
}

int Bag::pack_add_money(const Money_Add_Info &info, Money_Opt_Type type) {
	if (info.nums <= 0) {
		MSG_USER_TRACE("add nums : %d", info.nums);
		return ERROR_SERVER_INNER;
	}

	switch (info.type) {
	case BIND_COPPER:
		bag_info_.money_info.bind_copper += info.nums;
		break;
	case COPPER:
		bag_info_.money_info.copper += info.nums;
		break;
	case COUPON:
		bag_info_.money_info.coupon += info.nums;
		break;
	case GOLD:
		bag_info_.money_info.gold += info.nums;
		break;
	default:
		MSG_USER("add_type param error.");
		return -1;
	}

	if (0 != this->pack_set_money(bag_info_.money_info, type)) {
		return ERROR_MONEY_OVER_LIMIT;
	}

	return 0;
}
int Bag::pack_sub_money(const Money_Sub_Info &info, Money_Opt_Type type) {

	if (info.nums <= 0) {
		MSG_USER_TRACE("sub nums : %d", info.nums);
		return ERROR_SERVER_INNER;
	}

	switch (info.type) {
	case BIND_COPPER_FIRST:
		if (is_money_lock(BIND_COPPER)) {
			return ERROR_PACK_LOCK;
		}
		if ((int64_t)bag_info_.money_info.bind_copper + (int64_t)bag_info_.money_info.copper < info.nums) {
			return ERROR_ALL_COPPER_NOT_ENOUGH;
		} else if (bag_info_.money_info.bind_copper < info.nums) {
			if (is_money_lock(COPPER)) {
				return ERROR_PACK_LOCK;
			}
			bag_info_.money_info.copper -= info.nums - bag_info_.money_info.bind_copper;
			bag_info_.money_info.bind_copper = 0;
		} else {
			bag_info_.money_info.bind_copper -= info.nums;
		}
		break;
	case BIND_COPPER_ONLY: {
		if (is_money_lock(BIND_COPPER)) {
			return ERROR_PACK_LOCK;
		}
		bag_info_.money_info.bind_copper -= info.nums;
		break;
	}
	case COPPER_ONLY:
		if (is_money_lock(COPPER)) {
			return ERROR_PACK_LOCK;
		}
		bag_info_.money_info.copper -= info.nums;
		break;
	case GOLD_ONLY:
		if (is_money_lock(GOLD)) {
			return ERROR_PACK_LOCK;
		}
		bag_info_.money_info.gold -= info.nums;
		break;
	case COUPON_ONLY:
		if (is_money_lock(COUPON)) {
			return ERROR_PACK_LOCK;
		}
		bag_info_.money_info.coupon -= info.nums;
		break;
	default:
		MSG_USER("sub_type param error.");
		return -1;
	}

	if (0 != this->pack_set_money(bag_info_.money_info, type)) {
		switch (info.type) {
		case BIND_COPPER_FIRST:
			return ERROR_ALL_COPPER_NOT_ENOUGH;
		case COPPER_ONLY:
			return ERROR_COPPER_NOT_ENOUGH;
		case BIND_COPPER_ONLY:
			return ERROR_BIND_COPPER_NOT_ENOUGH;
		case COUPON_ONLY:
			return ERROR_COUPON_NOT_ENOUGH;
		case GOLD_ONLY:
			return ERROR_GOLD_NOT_ENOUGH;
		}
	}

	return 0;
}

inline void Bag::pack_get_iter(const Packer_Type pack_type, Bag_Info::Item_Map::iterator &begin, Bag_Info::Item_Map::iterator &end) {
	switch (pack_type) {
	case PACKER_T_ALL_INDEX:
		begin = bag_info_.item_map.begin();
		end = bag_info_.item_map.end();
		break;
	case PACKER_T_EQUIP_INDEX:
	case PACKER_T_PACKAGE_INDEX:
	case PACKER_T_STORAGE_INDEX:
		begin = bag_info_.item_map.upper_bound(pack_type);
		end = bag_info_.item_map.upper_bound(pack_type + pack_get_capacity(pack_type));
		break;
	default:
		begin = end;
		MSG_USER("pack_type param error.");
		break;
	}
}

int Bag::pack_get_capacity(const Packer_Type pack_type) {
	uint32_t capacity = 0;
	switch (pack_type) {
	case PACKER_T_EQUIP_INDEX:
		capacity = bag_info_.capacity.equip_cap;
		break;
	case PACKER_T_PACKAGE_INDEX:
		capacity = bag_info_.capacity.pack_cap;
		break;
	case PACKER_T_STORAGE_INDEX:
		capacity = bag_info_.capacity.storage_cap;
		break;
	default:
		break;
	}
	return capacity;
}

int Bag::pack_get_size(const Packer_Type pack_type) {
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);
	return std::distance(it_begin, it_end);
}

void Bag::clear_item(Packer_Type pack_type) {
	switch (pack_type) {
	case PACKER_T_PACKAGE_INDEX:
	case PACKER_T_EQUIP_INDEX:
	case PACKER_T_STORAGE_INDEX:
		break;
	default:
		return;
	}

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type, it_begin, it_end);

	bag_info_.erase(it_begin, it_end);
}

int Bag::pack_fetch_other_equip_info(Block_Buffer &basic_buf, Block_Buffer &addit_buf, int offset) {
	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(PACKER_T_EQUIP_INDEX, it_begin, it_end);

	MSG_300100 basic_msg;
	MSG_300101 addit_msg;

	for (Bag_Info::Item_Map::iterator item_it = it_begin; item_it != it_end; ++item_it) {
		pack_insert_item_to_msg(*item_it->second, basic_msg, addit_msg, offset);
	}
	basic_msg.serialize(basic_buf);
	addit_msg.serialize(addit_buf);
	return 0;
}

void Bag::lock_money(Money_Type type) {
	bag_info_.money_lock_map[type] = Time_Value::gettimeofday().sec();
}

void Bag::lock_item(uint32_t index) {
	bag_info_.item_lock_map[index] = Time_Value::gettimeofday().sec();
}

void Bag::unlock_money(Money_Type type) {
	bag_info_.money_lock_map.erase(type);
}

void Bag::unlock_item(uint32_t index) {
	bag_info_.item_lock_map.erase(index);
}

// 同一时间被锁定的格子，金钱不会很多，而且正常状态下，都会在很短情况下就解锁，不会超时再解锁，所以不用太关注这里的效率
int Bag::time_up(Time_Value &now) {
	if (!is_money_lock() && !is_item_lock()) {
		return 0;
	}

	for (Int_Int_Map::iterator it = bag_info_.money_lock_map.begin();
			it != bag_info_.money_lock_map.end();) {
		if (it->second + MAX_LOCK_SEC < now.sec()) {
			it = bag_info_.money_lock_map.erase(it);
		} else {
			++it;
		}
	}

	for (Int_Int_Map::iterator it = bag_info_.item_lock_map.begin();
			it != bag_info_.item_lock_map.end();) {
		if (it->second + MAX_LOCK_SEC < now.sec()) {
			it = bag_info_.item_lock_map.erase(it);
		} else {
			++it;
		}
	}

	return 0;
}

inline void Bag::pack_get_index(const Packer_Type pack_type, uint32_t &begin, uint32_t &end) {
	switch (pack_type) {
	case PACKER_T_EQUIP_INDEX:
		begin = PACKER_T_EQUIP_INDEX + 1;
		end = begin + bag_info_.capacity.equip_cap;
		break;
	case PACKER_T_PACKAGE_INDEX:
		begin = PACKER_T_PACKAGE_INDEX + 1;
		end = begin + bag_info_.capacity.pack_cap;
		break;
	case PACKER_T_STORAGE_INDEX:
		begin = PACKER_T_STORAGE_INDEX + 1;
		end = begin + bag_info_.capacity.storage_cap;
		break;
	default:
		begin = 0;
		end = 0;
		break;
	}
}

int Bag::pack_try_insert_item(const Packer_Type pack_type, const Item_Info &item) {
	return pack_try_insert_item(pack_type, std::vector<Item_Info>(1, item));
}

int Bag::pack_get_buy_power(const Money_Sub_Type sub_type, int unit_price) {
	if (unit_price <= 0) return 0;
	int64_t total_money;
	switch (sub_type) {
	case BIND_COPPER_FIRST:
		total_money = (int64_t)bag_info_.money_info.bind_copper + (int64_t)bag_info_.money_info.copper;
		break;
	case COPPER_ONLY:
		total_money = bag_info_.money_info.copper;
		break;
	case GOLD_ONLY:
		total_money = bag_info_.money_info.gold;
		break;
	case COUPON_ONLY:
		total_money = bag_info_.money_info.coupon;
		break;
	default:
		total_money = 0;
		break;
	}

	return total_money / unit_price;
}

int Bag::pack_try_sub_money(const Money_Sub_Info &info) {
	Money_Info money_info = bag_info_.money_info;
	int result = pack_sub_money(info, MONEY_OPT_TRY);
	if (result != 0) return result;
	bag_info_.money_info = money_info;
	return 0;
}

int Bag::pack_try_erase_item(const Packer_Type pack_type, const Id_Amount &id_amount) {
	if (id_amount.amount <= 0) return ERROR_CLIENT_OPERATE;
	if (pack_calc_item(pack_type, id_amount.id, id_amount.bind_verify) < id_amount.amount) {
		return ERROR_ITEM_NOT_ENOUGH;
	}
	if (is_item_lock()) { // 存在物品被锁定的情况下，要检测是否有物品被锁定导致不能扣除
		if (pack_calc_unlock_item(pack_type, id_amount.id, id_amount.bind_verify) < id_amount.amount) {
			return ERROR_PACK_LOCK;
		}
	}
	return 0;
}

int Bag::pack_try_add_money(const Money_Add_Info &info) {
	Money_Info money_info = bag_info_.money_info;
	int result = pack_add_money(info, MONEY_OPT_TRY);
	if (result != 0) return result;
	bag_info_.money_info = money_info;
	return 0;
}

int Bag::pack_erase_item(const uint32_t index, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(index, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_erase_item(const uint32_t index, UInt_Set* changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_erase_item(index);
		if (result != 0) return result;
	}

	Bag_Info::Item_Map::iterator it = bag_info_.item_map.find(index);
	bag_info_.erase(it);
	if (NULL != changed_set) {
		changed_set->insert(index);
	}

	return 0;
}

int Bag::pack_try_erase_item(const Index_Amount &index_amount) {
	if (is_item_lock(index_amount.index)) {
		return ERROR_PACK_LOCK;
	}

	if (index_amount.amount <= 0) {
		return ERROR_CLIENT_OPERATE;
	}

	const Item_Info *item = pack_get_const_item(index_amount.index);
	// 检测存在物品
	if (NULL == item) return ERROR_ITEM_NOT_EXIST;
	// 检测物品数量
	if (pack_is_req_item(*item, item->item_basic.id, index_amount.bind_verify)) {
		if (index_amount.amount > item->item_basic.amount) return ERROR_ITEM_NOT_ENOUGH;
	} else {
		if (index_amount.bind_verify == UNBIND_ONLY) return ERROR_UNBIND_ITEM_NOT_ENOUGH;
		if (index_amount.bind_verify == BIND_ONLY) return ERROR_BIND_ITEM_NOT_ENOUGH;
	}

	return 0;
}

int Bag::pack_try_erase_item(const std::vector<Index_Amount> &index_amount_list) {
	// 合并重复index
	std::map<Index_Amount, int32_t> amount_map;
	for (std::vector<Index_Amount>::const_iterator it = index_amount_list.begin(); it != index_amount_list.end(); ++it) {
		if (it->amount > 0) {
			amount_map[*it] += it->amount;
		} else {
			return ERROR_CLIENT_OPERATE;
		}
	}

	for (std::map<Index_Amount, int32_t>::iterator map_it = amount_map.begin(); map_it != amount_map.end(); ++map_it) {
		int result = pack_try_erase_item(Index_Amount(map_it->first.index, map_it->second, map_it->first.bind_verify));
		if (result != 0) return result;
	}

	return 0;
}

int Bag::pack_erase_item(const std::vector<Index_Amount> &index_amount_list, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(index_amount_list, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_try_erase_item(const uint32_t index) {
	if (is_item_lock(index)) {
		return ERROR_PACK_LOCK;
	}
	if (!is_item_exist(index)) {
		return ERROR_ITEM_NOT_EXIST;
	}

	return 0;
}

int Bag::pack_try_erase_item(const UInt_Vec &index_list) {
	// 此处不用合并index，若有多次删除一个格子的情况，与删除一次的效果是一样的
	for (UInt_Vec::const_iterator it = index_list.begin(); it != index_list.end(); ++it) {
		int result = pack_try_erase_item(*it);
		if (result != 0) return result;
	}

	return 0;
}

int Bag::pack_erase_item(const UInt_Vec &index_list, Pack_Try pack_try) {
	int result = 0;
	UInt_Set changed_set;
	result = this->pack_erase_item(index_list, &changed_set, pack_try);

	if (result == 0) {
		result = pack_active_update_item_detail(&changed_set);
	}

	return result;
}

int Bag::pack_erase_item(const std::vector<Index_Amount> &index_amount_list, UInt_Set* changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_erase_item(index_amount_list);
		if (result != 0) return result;
	}

	for (std::vector<Index_Amount>::const_iterator it = index_amount_list.begin(); it != index_amount_list.end(); ++it) {
		pack_erase_item(*it, changed_set, WITHOUT_TRY);
	}

	return 0;
}

int Bag::pack_erase_item(const UInt_Vec &index_list,  UInt_Set* changed_set, Pack_Try pack_try) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_erase_item(index_list);
		if (result != 0) return result;
	}

	for (UInt_Vec::const_iterator it = index_list.begin(); it != index_list.end(); ++it) {
		pack_erase_item(*it, changed_set, WITH_TRY);
	}

	return 0;
}

int Bag::pack_try_move_item(const uint32_t index_from, const uint32_t index_to) {
	Packer_Type type;
	if (0 != pack_get_pack_type(index_to, type)) {
		return ERROR_PACK_INDEX_NOT_EXIST;
	}

	if (!is_item_exist(index_from)) {
		return ERROR_ITEM_NOT_EXIST;
	}
	if (is_item_lock(index_from) || is_item_lock(index_to)) {
		return ERROR_PACK_LOCK;
	}

	return 0;
}

int Bag::pack_move_item(const uint32_t index_from, const uint32_t index_to, Pack_Try pack_try ) {
	if (pack_try == WITH_TRY) {
		int result = pack_try_move_item(index_from, index_to);
		if (result != 0) return result;
	}

	Bag_Info::Item_Map::iterator it_from = bag_info_.item_map.find(index_from);
	Bag_Info::Item_Map::iterator it_to = bag_info_.item_map.find(index_to);

	if (it_to == bag_info_.item_map.end()) {
		bag_info_.item_map.insert(std::make_pair(index_to, it_from->second));
		bag_info_.item_map.erase(it_from);
	} else {
		uint32_t id_to = it_to->second->item_basic.id;
		int32_t stack_upper_to = Item_Info::get_item_stack_upper(id_to);
		int32_t remain_space = stack_upper_to - it_to->second->item_basic.amount;
		if (!is_equal_item(*it_from->second, *it_to->second) || stack_upper_to <= 1) {
			std::swap(it_to->second, it_from->second);
		} else if (remain_space == 0){
			return 0;
		} else{
			pack_merge_equal_item(*it_to->second, *it_from->second);
			if (it_from->second->item_basic.amount == 0) {
				bag_info_.erase(it_from);
			}
		}
	}

	// index不交换
	it_to = bag_info_.item_map.find(index_to);
	if (it_to != bag_info_.item_map.end()) {
		it_to->second->item_basic.index = index_to;
	}
	it_from = bag_info_.item_map.find(index_from);
	if (it_from != bag_info_.item_map.end()) {
		it_from->second->item_basic.index = index_from;
	}

	UInt_Set changed_set;
	changed_set.insert(index_to);
	changed_set.insert(index_from);

	return this->pack_active_update_item_detail(&changed_set);
}

int Bag::pack_try_move_item(const uint32_t index_from, const Packer_Type pack_type_to) {
	// 不允许移动至当前包裹
	if (pack_is_pack_type(index_from, pack_type_to) == 0) return ERROR_CLIENT_PARAM;
	if (is_item_lock(index_from)) {
		return ERROR_PACK_LOCK;
	}
	const Item_Info* item = pack_get_const_item(index_from);
	if (NULL == item) return ERROR_ITEM_NOT_EXIST;

	return pack_try_insert_item(pack_type_to, *item);
}

int Bag::pack_try_move_item(const Packer_Type pack_type_from, const Packer_Type pack_type_to) {
	if (pack_type_to == PACKER_T_EQUIP_INDEX) return ERROR_CLIENT_PARAM;

	if (is_item_lock()) {
		return ERROR_PACK_LOCK;
	}

	Bag_Info::Item_Map::iterator it_begin;
	Bag_Info::Item_Map::iterator it_end;
	pack_get_iter(pack_type_from, it_begin, it_end);

	UInt_Set changed_set;
	std::vector<Item_Info> item_list;
	for (Bag_Info::Item_Map::iterator it = it_begin; it != it_end; ++it) {
		item_list.push_back(*it->second);
	}

	return pack_try_insert_item(pack_type_to, item_list);
}

int Bag::pack_try_add_money(const Money_Add_List &money_add_list) {
	Money_Info money_info = bag_info_.money_info;
	int result = pack_add_money(money_add_list, MONEY_OPT_TRY);
	if (result != 0) return result;
	bag_info_.money_info = money_info;
	return 0;
}

int Bag::pack_add_money(const Money_Add_List &money_add_list, Money_Opt_Type type) {
	Money_Info old_money_detail = bag_info_.money_info;
	int result = 0;
	Money_Add_List temp_add_list;
	for (Money_Add_List::const_iterator it = money_add_list.begin(); it != money_add_list.end(); ++it) {
		if (it->nums > 0) {
			temp_add_list.push_back(*it);
		}
	}
	if (temp_add_list.empty()) {
		MSG_USER_TRACE("add_list empty");
		return ERROR_SERVER_INNER;
	}
	for (Money_Add_List::const_iterator it = temp_add_list.begin(); it != temp_add_list.end(); ++it) {
		result = pack_add_money(*it, MONEY_OPT_TRY);
		if (result != 0) break;
	}

	// 回滚
	bag_info_.money_info = old_money_detail;
	if (result != 0) {
		return result;
	}

	for (Money_Add_List::const_iterator it = temp_add_list.begin(); it != temp_add_list.end(); ++it) {
		pack_add_money(*it, type);
	}

	return 0;
}

int Bag::pack_try_sub_money(const Money_Sub_List &money_sub_list) {
	Money_Info money_info = bag_info_.money_info;
	int result = pack_sub_money(money_sub_list, MONEY_OPT_TRY);
	if (result != 0) return result;
	bag_info_.money_info = money_info;
	return 0;
}

int Bag::pack_sub_money(const Money_Sub_List &money_sub_list, Money_Opt_Type type) {
	Money_Info old_money_detail = bag_info_.money_info;
	int result = 0;
	Money_Sub_List temp_sub_list;
	for (Money_Sub_List::const_iterator it = money_sub_list.begin(); it != money_sub_list.end(); ++it) {
		if (it->nums > 0) {
			temp_sub_list.push_back(*it);
		}
	}
	if (temp_sub_list.empty()) {
		MSG_USER_TRACE("sub_list empty");
		return ERROR_SERVER_INNER;
	}
	for (Money_Sub_List::const_iterator it = temp_sub_list.begin(); it != temp_sub_list.end(); ++it) {
		result = pack_sub_money(*it, MONEY_OPT_TRY);
		if (result != 0) break;
	}

	// 回滚
	bag_info_.money_info = old_money_detail;
	if (result != 0) {
		return result;
	}

	for (Money_Sub_List::const_iterator it = temp_sub_list.begin(); it != temp_sub_list.end(); ++it) {
		pack_sub_money(*it, type);
	}

	return 0;
}

void Bag::recovery() {
	role_id_t role_id = bag_info_.role_id;
	bag_info_.reset();
	bag_info_.role_id = role_id;
}