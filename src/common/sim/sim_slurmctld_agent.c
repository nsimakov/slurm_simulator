#include "src/slurmctld/agent.c"

extern void __real_agent_queue_request(agent_arg_t *agent_arg_ptr);
extern void __wrap_agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	debug("Sim: __wrap_agent_queue_request msg_type=%s", rpc_num2string(agent_arg_ptr->msg_type));
	__real_agent_queue_request(agent_arg_ptr);
}
