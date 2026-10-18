#include "UndoStack.h"

namespace hyp::ed
{
    void UndoStack::push(HString name, std::function<void()> undo, std::function<void()> redo)
    {
        mUndoStack.push_back(Command{ name, std::move(undo), std::move(redo) });
        if (mUndoStack.size() > kMaxDepth)
            mUndoStack.pop_front();
        mRedoStack.clear();
    }

    void UndoStack::undo()
    {
        if (mUndoStack.empty())
            return;

        Command command = std::move(mUndoStack.back());
        mUndoStack.pop_back();
        command.undo();
        mRedoStack.push_back(std::move(command));
    }

    void UndoStack::redo()
    {
        if (mRedoStack.empty())
            return;

        Command command = std::move(mRedoStack.back());
        mRedoStack.pop_back();
        command.redo();
        mUndoStack.push_back(std::move(command));
    }

    HString UndoStack::peekUndoName() const noexcept
    {
        return mUndoStack.empty() ? HString() : mUndoStack.back().name;
    }

    HString UndoStack::peekRedoName() const noexcept
    {
        return mRedoStack.empty() ? HString() : mRedoStack.back().name;
    }

    void UndoStack::clear() noexcept
    {
        mUndoStack.clear();
        mRedoStack.clear();
        mGestureSnapshots.clear();
    }
}
