local utils = require "_utils"

-- expand _utils module with Lua5.4 specific stuff

-- a lane body that yields stuff
utils.yielder_with_to_be_closed = function(out_linda_, wait_)
	local fixture = assert(require "fixture")
	-- here is a to-be-closed variable that, when closed, sends "Closed!" in the "out" slot of the provided linda
	local t <close> = setmetatable(
		{ text = "Closed!" }, {
			__close = function(self, err)
				if wait_ then
					fixture.block_for(wait_)
				end
				out_linda_:send("out", self.text)
			end
		}
	)
	-- yield forever, but be cancel-friendly
	local n = 1
	while true do
		coroutine.yield("I yield!", n)
		if cancel_test and cancel_test() then -- cancel_test does not exist when run immediately (not in a Lane)
			return "I am cancelled"
		end
		n = n + 1
	end
end

return utils
