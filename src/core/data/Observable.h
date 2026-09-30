#ifndef HGE_OBSERVABLE_H
#define HGE_OBSERVABLE_H

#include "../Common.h"
#include <functional>
#include <utility>

class LYNX_API IObservable {
public:
	virtual ~IObservable()=default;
	virtual void Tick()=0;
};


//not ENGINE_API because of the template
template<typename T>
class Observable : public IObservable {
public:
	Observable(T& ref, std::function<void()> callback)
			: last_(ref),
				ref_(ref),
				callback_(std::move(callback))
	{
	}

	void Tick() final
	{
		if (last_ != ref_)
		{
			callback_();
			last_ = ref_;
		}
	}

private:
	T last_;
	T& ref_;
	std::function<void()> callback_;
};

#endif