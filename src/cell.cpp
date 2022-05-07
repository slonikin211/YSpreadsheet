#include "cell.h"

#include <cassert>
#include <iostream>
#include <string>
#include <optional>

using namespace std::literals;

class Cell::Impl
{
public:
	virtual Value GetValue() const = 0;
	virtual std::string GetText() const = 0;
	virtual std::optional<FormulaInterface::Value> GetCache() const = 0;
	virtual std::vector<Position> GetReferencedCells() const = 0;
	virtual void DestroyCache() = 0;
};

class Cell::EmptyImpl : public Cell::Impl
{
public:
	Value GetValue()const override{
		return ""s; 
	}
	std::string GetText() const override{
		return ""s; 
	}
	std::optional<FormulaInterface::Value> GetCache() const override{
		return std::nullopt;
	}
	std::vector<Position> GetReferencedCells() const override {
		return {};
	}
	void DestroyCache() override {}
};

class Cell::TextImpl : public Cell::Impl
{
public:
	TextImpl(std::string text) : text_(std::move(text)) {
		if (text_.empty()) { throw std::logic_error("Here is no text"); }
	}
	Value GetValue() const override {
		return text_.at(0) == ESCAPE_SIGN ? text_.substr(1u) : text_;
	}
	std::string GetText() const override { 
		return text_; 
	}
	std::optional<FormulaInterface::Value> GetCache() const override {
		return std::nullopt;
	}
	std::vector<Position> GetReferencedCells() const override {
		return {};
	}
	void DestroyCache() override {}

private:
	std::string text_;
};

class Cell::FormulaImpl : public Cell::Impl
{
public:
	FormulaImpl(std::string expression, const SheetInterface& sheet) : sheet_(sheet) {
		if (expression.empty() || expression.at(0u) != FORMULA_SIGN) {
			throw std::logic_error("Something wrong with formula implementation");
		}
		formula_ = ParseFormula(expression.substr(1u));
	}

	Value GetValue() const override {
		if (!cache_) {
			cache_ = formula_->Evaluate(sheet_);
		}
		return std::visit([](const auto& x) { return Value(x); }, *cache_);
	}

	std::string GetText() const override {
		return FORMULA_SIGN + formula_->GetExpression();
	}

	std::vector<Position> GetReferencedCells() const override {
		return formula_->GetReferencedCells();
	}

	void DestroyCache() override {
		cache_.reset();
	}

	std::optional<FormulaInterface::Value> GetCache() const override {
		return cache_;
	}

private:
	const SheetInterface& sheet_;
	std::unique_ptr<FormulaInterface> formula_;
	mutable std::optional<FormulaInterface::Value> cache_;
};

Cell::Cell(SheetInterface& sheet) : impl_(std::make_unique<EmptyImpl>()), sheet_(sheet) {}

Cell::~Cell() {}

void Cell::Set(std::string text) {
	std::unique_ptr<Impl> impl;

	if (text.empty()) {
		impl = std::make_unique<EmptyImpl>();
	}
	else if (text.size() > 1 && text[0] == FORMULA_SIGN) {
		impl = std::make_unique<FormulaImpl>(std::move(text), sheet_);
	}
	else {
		impl = std::make_unique<TextImpl>(std::move(text));
	}

	CheckCircularDependency(impl->GetReferencedCells());
	impl_ = std::move(impl);

	UpdateDependencies();
	DestroyCache(this);
}

void Cell::Clear() {
	impl_ = std::make_unique<EmptyImpl>();
}

Cell::Value Cell::GetValue() const {
	return impl_->GetValue();
}

std::string Cell::GetText() const {
	return impl_->GetText();
}

std::vector<Position> Cell::GetReferencedCells() const {
	return impl_->GetReferencedCells();
}

bool Cell::IsReferenced() const
{
	return false;
}

void Cell::UpdateDependencies()
{
	for (Cell* cell : dependent_cells_) {
		cell->referenced_cells_.erase(this);
	}
	referenced_cells_.clear();

	for (const auto& pos : impl_->GetReferencedCells()) {
		CellInterface* ref_cell = sheet_.GetCell(pos);

		if (!ref_cell) {
			sheet_.SetCell(pos, ""s);
			ref_cell = sheet_.GetCell(pos);
		}
		referenced_cells_.insert(static_cast<Cell*>(ref_cell));
		static_cast<Cell*>(ref_cell)->dependent_cells_.insert(this);
	}
}

void Cell::CheckCircularDependency(const std::vector<Position>& ref_cells) const
{
	std::unordered_set<const CellInterface*> visited;
	CheckCircularDependencyImpl(visited, ref_cells);
}

void Cell::CheckCircularDependencyImpl(std::unordered_set<const CellInterface*>& visited, const std::vector<Position>& positions) const
{
	for (const auto& pos : positions) {
		CellInterface* cell = sheet_.GetCell(pos);
		if (cell == this) {
			throw CircularDependencyException("Circular cell");
		}
		if (cell && !visited.count(cell)) {
			const auto ref_cells = cell->GetReferencedCells();
			if (!ref_cells.empty()) {
				CheckCircularDependencyImpl(visited, ref_cells);
			}
			visited.insert(cell);
		}
	}
}

void Cell::DestroyCache(Cell* cell)
{
	for (Cell* dep_cell : cell->dependent_cells_) {
		if (dep_cell->impl_->GetCache()) {
			DestroyCache(dep_cell);
			dep_cell->impl_->DestroyCache();
		}
	}
}
