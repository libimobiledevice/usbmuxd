import asyncio
from composio import Composio
from agents import Agent, Runner
from composio_openai_agents import OpenAIAgentsProvider

# Initialize Composio with OpenAI Agents provider
composio = Composio(api_key="ak_QXZfUYIDuxvR5NpZc1ZT", provider=OpenAIAgentsProvider())

external_user_id = "pg-test-26834860-06fa-4bf2-be9e-35b375db685d"

# Create a tool router session
session = composio.create(
    user_id=external_user_id,
)

# Get tools from the session (native)
tools = session.tools()

# Create agent with tools
agent = Agent(
    name="Email Manager",
    instructions="You are a helpful assistant that helps users manage their Gmail accounts.",
    tools=tools,
)

# Run the agent
async def main():
    result = await Runner.run(
        starting_agent=agent,
        input="Send an email to almaperezlopez2510@gmail.com with the subject 'Hello from Composio' and the body 'This is a test email!'",
    )
    print(result.final_output)

if __name__ == "__main__":
    asyncio.run(main())